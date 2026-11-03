package main

import (
	"net/http"
	"sort"
	"strconv"
	"sync"
	"time"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/history"
)

// historyReader is the history the run totals sum: the index rows to filter
// before opening, and the fight opener. *history.Store is the live one; the
// tests fake it to count how many fights the handler opens.
type historyReader interface {
	ListAll(commander string) []history.Row
	Fight(key string) (combat.Fight, history.Row, bool)
}

// runTotalsCache holds one summed run per (commander, session), at most
// runTotalsCacheCap entries: the live run for ten seconds, an ended run (no
// new fights in ten minutes and no fight still running) until a newer fight
// lands. Every hit compares the entry's freshness marker against the current
// index rows for that commander and session and recomputes when it differs.
const runTotalsCacheCap = 256

type runTotalsCache struct {
	mu      sync.Mutex
	entries map[string]*runTotalsEntry
	head    *runTotalsEntry
	tail    *runTotalsEntry
	len     int
}

type runTotalsEntry struct {
	key    string
	totals combat.RunTotals
	more   bool
	at     time.Time
	last   int64
	n      int
	end    bool
	prev   *runTotalsEntry
	next   *runTotalsEntry
}

func (c *runTotalsCache) touch(e *runTotalsEntry) {
	if c.head == e {
		return
	}
	if e.prev != nil {
		e.prev.next = e.next
	}
	if e.next != nil {
		e.next.prev = e.prev
	}
	if c.tail == e {
		c.tail = e.prev
	}
	e.prev = nil
	e.next = c.head
	if c.head != nil {
		c.head.prev = e
	}
	c.head = e
	if c.tail == nil {
		c.tail = e
	}
}

func (c *runTotalsCache) evict(e *runTotalsEntry) {
	if e.prev != nil {
		e.prev.next = e.next
	} else {
		c.head = e.next
	}
	if e.next != nil {
		e.next.prev = e.prev
	} else {
		c.tail = e.prev
	}
	e.prev, e.next = nil, nil
	delete(c.entries, e.key)
	c.len--
}

func (c *runTotalsCache) get(key string, last int64, n int, now time.Time) (combat.RunTotals, bool, bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.entries == nil {
		return combat.RunTotals{}, false, false
	}
	e, ok := c.entries[key]
	if !ok {
		return combat.RunTotals{}, false, false
	}
	if e.last != last || e.n != n {
		c.evict(e)
		return combat.RunTotals{}, false, false
	}
	if !e.end && now.Sub(e.at) > 10*time.Second {
		c.evict(e)
		return combat.RunTotals{}, false, false
	}
	c.touch(e)
	return e.totals, e.more, true
}

func (c *runTotalsCache) set(key string, totals combat.RunTotals, more bool, last int64, n int, end bool, now time.Time) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.entries == nil {
		c.entries = map[string]*runTotalsEntry{}
	}
	if e, ok := c.entries[key]; ok {
		e.totals, e.more, e.at, e.last, e.n, e.end = totals, more, now, last, n, end
		c.touch(e)
		return
	}
	e := &runTotalsEntry{key: key, totals: totals, more: more, at: now, last: last, n: n, end: end}
	c.entries[key] = e
	e.next = c.head
	if c.head != nil {
		c.head.prev = e
	}
	c.head = e
	if c.tail == nil {
		c.tail = e
	}
	c.len++
	for c.len > runTotalsCacheCap {
		c.evict(c.tail)
	}
}

var runTotalsLive = &runTotalsCache{}

// runTotalsNow is the clock the cache reads; the tests park it.
var runTotalsNow = time.Now

// runTotalsHandler serves GET /api/v1/combat/run-totals?commander=&session=:
// the finished fights of that commander's run (FightSummary.session), summed
// per bot from the memory store plus the history files (de-duplicated by
// fight key, the same readers the list routes use). At most
// combat.RunTotalsCap fights are summed; a cut answers X-More: 1. A missing
// or bad commander or session is a 400.
func runTotalsHandler(combatStore *combat.Store, historyStore historyReader) http.HandlerFunc {
	return runTotalsHandlerCap(combatStore, historyStore, combat.RunTotalsCap)
}

func runTotalsHandlerCap(combatStore *combat.Store, historyStore historyReader, limit int) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		commander := q.Get("commander")
		session, serr := strconv.ParseUint(q.Get("session"), 10, 64)
		if commander == "" || serr != nil || q.Get("session") == "" {
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusBadRequest)
			_, _ = w.Write([]byte(`{"error":"bad commander or session"}`))
			return
		}
		now := runTotalsNow()
		key := commander + "\x00" + strconv.FormatUint(session, 10)
		last, live, n := runMark(combatStore, historyStore, commander, session)
		if totals, more, ok := runTotalsLive.get(key, last, n, now); ok {
			if more {
				w.Header().Set("X-More", "1")
			}
			writeJSON(w, totals)
			return
		}
		in := collectRun(combatStore, historyStore, commander, session, limit)
		totals, more := combat.SumRun(in, limit)
		// No ended entry while a fight of this run is still live: the row it
		// becomes may land any moment, and the entry would freeze it out.
		end := last > 0 && !live && now.Unix()-last > 600
		if last == 0 {
			writeJSON(w, totals)
			return
		}
		runTotalsLive.set(key, totals, more, last, n, end, now)
		if more {
			w.Header().Set("X-More", "1")
		}
		writeJSON(w, totals)
	}
}

// runMark is the freshness marker of one run: the newest fight start, whether
// a fight is still live, and how many finished rows the run holds. From the
// index rows and the memory list alone: no fight is opened.
func runMark(combatStore *combat.Store, historyStore historyReader, commander string, session uint64) (int64, bool, int) {
	var last int64
	var live bool
	var n int
	if combatStore != nil {
		for _, row := range combatStore.List(commander) {
			if row.Session != session {
				continue
			}
			if row.Live {
				live = true
				continue
			}
			n++
			if row.Start > last {
				last = row.Start
			}
		}
	}
	if historyStore != nil {
		for _, row := range historyStore.ListAll(commander) {
			if row.Session != session {
				continue
			}
			n++
			if row.Start > last {
				last = row.Start
			}
		}
	}
	return last, live, n
}

// collectRun gathers one run's fights: the finished memory rows of that
// session (no live rows: an ended-but-unfiled row has an outcome and no
// per-bot numbers, and a still-open one carries a partial window; neither is
// a finished fight), then the matching history index rows newest first (only
// those are opened), one past the cap so SumRun learns the cut.
func collectRun(combatStore *combat.Store, historyStore historyReader, commander string, session uint64, limit int) []combat.RunInput {
	var in []combat.RunInput
	seen := map[string]bool{}
	if combatStore != nil {
		for _, row := range combatStore.List(commander) {
			if row.Session != session || row.Live {
				continue
			}
			f, _, ok := combatStore.GetKey(row.Key)
			if !ok || len(f.Bots) == 0 {
				continue
			}
			in = append(in, combat.RunInput{Key: row.Key, Fight: f})
			seen[row.Key] = true
		}
	}
	if historyStore == nil {
		return in
	}
	rows := historyStore.ListAll(commander)
	matched := rows[:0]
	for _, row := range rows {
		if row.Commander != commander || row.Session != session {
			continue
		}
		matched = append(matched, row)
	}
	sort.Slice(matched, func(i, j int) bool {
		if matched[i].Start != matched[j].Start {
			return matched[i].Start > matched[j].Start
		}
		return matched[i].Key < matched[j].Key
	})
	opened := len(in)
	for _, row := range matched {
		if opened > limit {
			break
		}
		if seen[row.Key] {
			continue
		}
		f, _, ok := historyStore.Fight(row.Key)
		if !ok || len(f.Bots) == 0 {
			continue
		}
		in = append(in, combat.RunInput{Key: row.Key, Fight: f})
		seen[row.Key] = true
		opened++
	}
	return in
}
