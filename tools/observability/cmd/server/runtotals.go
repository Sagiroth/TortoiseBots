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

// runTotalsCache holds one summed run per (commander, session): the live run
// for ten seconds, an ended run (no new fights in ten minutes) for good.
type runTotalsCache struct {
	mu      sync.Mutex
	now     func() time.Time
	entries map[string]runTotalsEntry
}

type runTotalsEntry struct {
	totals combat.RunTotals
	more   bool
	at     time.Time
	last   int64
	end    bool
}

func (c *runTotalsCache) get(key string, last int64, now time.Time) (combat.RunTotals, bool, bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	e, ok := c.entries[key]
	if !ok {
		return combat.RunTotals{}, false, false
	}
	if e.end {
		return e.totals, e.more, true
	}
	if now.Sub(e.at) > 10*time.Second || e.last != last {
		return combat.RunTotals{}, false, false
	}
	return e.totals, e.more, true
}

func (c *runTotalsCache) set(key string, totals combat.RunTotals, more bool, last int64, end bool, now time.Time) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.entries == nil {
		c.entries = map[string]runTotalsEntry{}
	}
	c.entries[key] = runTotalsEntry{totals: totals, more: more, at: now, last: last, end: end}
}

var runTotalsLive = &runTotalsCache{}

// runTotalsNow is the clock the cache reads; the tests park it.
var runTotalsNow = time.Now

// runTotalsHandler serves GET /api/v1/combat/run-totals?commander=&session=:
// the finished fights of that commander's run (FightSummary.session), summed
// per bot from the memory store plus the history files (de-duplicated by
// fight key, the same readers the list routes use). since stays as an extra
// lower bound in unix milliseconds. At most combat.RunTotalsCap fights are
// summed; a cut answers X-More: 1. A missing or bad commander or session is
// a 400.
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
		var sinceS int64
		if s := q.Get("since"); s != "" {
			ms, err := strconv.ParseInt(s, 10, 64)
			if err != nil || ms < 0 {
				w.Header().Set("Content-Type", "application/json")
				w.WriteHeader(http.StatusBadRequest)
				_, _ = w.Write([]byte(`{"error":"bad since"}`))
				return
			}
			sinceS = ms / 1000
		}
		now := runTotalsNow()
		key := commander + "\x00" + strconv.FormatUint(session, 10)
		last := runLastStart(combatStore, historyStore, commander, session)
		if totals, more, ok := runTotalsLive.get(key, last, now); ok {
			if more {
				w.Header().Set("X-More", "1")
			}
			writeJSON(w, totals)
			return
		}
		in := collectRun(combatStore, historyStore, commander, session, sinceS, limit)
		totals, more := combat.SumRun(in, sinceS, limit)
		totals.Since = sinceS
		end := last > 0 && now.Unix()-last > 600
		runTotalsLive.set(key, totals, more, last, end, now)
		if more {
			w.Header().Set("X-More", "1")
		}
		writeJSON(w, totals)
	}
}

// runLastStart is the newest fight start of the run, from the index rows and
// the memory list alone: the cache's freshness check opens no fights.
func runLastStart(combatStore *combat.Store, historyStore historyReader, commander string, session uint64) int64 {
	var last int64
	if combatStore != nil {
		for _, row := range combatStore.List(commander) {
			if row.Session == session && row.Start > last {
				last = row.Start
			}
		}
	}
	if historyStore != nil {
		for _, row := range historyStore.ListAll(commander) {
			if row.Session == session && row.Start > last {
				last = row.Start
			}
		}
	}
	return last
}

// collectRun gathers one run's fights: the memory rows of that session, then
// the matching history index rows newest first (only those are opened), one
// past the cap so SumRun learns the cut. since is an extra lower bound on
// the fight start.
func collectRun(combatStore *combat.Store, historyStore historyReader, commander string, session uint64, since int64, limit int) []combat.RunInput {
	var in []combat.RunInput
	if combatStore != nil {
		for _, row := range combatStore.List(commander) {
			if row.Session != session || row.Start < since {
				continue
			}
			f, _, ok := combatStore.GetKey(row.Key)
			if !ok {
				continue
			}
			in = append(in, combat.RunInput{Key: row.Key, Fight: f})
		}
	}
	if historyStore == nil {
		return in
	}
	rows := historyStore.ListAll(commander)
	matched := rows[:0]
	for _, row := range rows {
		if row.Commander != commander || row.Session != session || row.Start < since {
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
		f, _, ok := historyStore.Fight(row.Key)
		if !ok {
			continue
		}
		in = append(in, combat.RunInput{Key: row.Key, Fight: f})
		opened++
	}
	return in
}
