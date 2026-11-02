// The HTTP side of the underperformers view: read one commander's fights
// from memory and from the fight history, fold them with Build, and fill the
// armory's spec names where a bot is listed.
package underperf

import (
	"encoding/json"
	"net/http"
	"sort"
	"strconv"
	"sync"
	"time"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/history"
)

// MemSource is the daemon's in-memory fight store.
type MemSource interface {
	List(commander string) []combat.FightSummary
	GetKey(key string) (combat.Fight, []combat.Warning, bool)
}

// HistSource is the fight history on disk.
type HistSource interface {
	ListAll(commander string) []history.Row
	Fight(key string) (combat.Fight, history.Row, bool)
}

// Sources is what the handler reads. Hist and Specs may be nil (history off,
// armory off); Rules may be nil (no warning rules).
type Sources struct {
	Mem   MemSource
	Hist  HistSource
	Rules func() []combat.Rule
	Specs func([]uint32) map[uint32]string
	Now   func() time.Time
}

type specEntry struct {
	spec string
	at   time.Time
}

// specTTL is how long a spec answer per guid is reused: a viewer polls this
// route every 30 s and their GUID set changes only when fights do.
const specTTL = 10 * time.Minute

// Handler serves GET /api/v1/combat/underperformers.
func Handler(src Sources) http.HandlerFunc {
	nowFn := src.Now
	if nowFn == nil {
		nowFn = time.Now
	}
	var mu sync.Mutex
	specs := map[uint32]specEntry{}

	return func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		commander := q.Get("commander")
		if commander == "" {
			writeErr(w, "commander is required")
			return
		}
		since, until, fallback, msg := window(q, nowFn())
		if msg != "" {
			writeErr(w, msg)
			return
		}
		if fallback {
			w.Header().Set("X-TZ-Fallback", "utc")
		}
		runSel := q.Get("run")
		if runSel == "" {
			runSel = "all"
		}
		if runSel != "all" && runSel != "last" {
			n, err := strconv.Atoi(runSel)
			if err != nil || n < 0 || strconv.Itoa(n) != runSel {
				writeErr(w, "bad run")
				return
			}
		}

		var rules []combat.Rule
		if src.Rules != nil {
			rules = src.Rules()
		}
		floors := ParseFloors(rules)
		lim := DefaultLimits()
		fights, preTruncated := collect(src, commander, since, until, rules, lim.MaxFights)
		base := Build(commander, since, until, fights, rules, floors, lim)
		base.Truncated = base.Truncated || preTruncated

		rep := base
		if runSel != "all" {
			idx := len(base.Runs) - 1
			if runSel != "last" {
				idx, _ = strconv.Atoi(runSel)
			}
			if idx < 0 || idx >= len(base.Runs) {
				writeErr(w, "run out of range")
				return
			}
			run := base.Runs[idx]
			var subset []Input
			for _, f := range fights {
				if f.Session == run.Session && f.Fight.Start >= run.Since && f.Fight.Start < run.Until {
					subset = append(subset, f)
				}
			}
			rep = Build(commander, run.Since, run.Until, subset, rules, floors, lim)
			// The base window's runs stay in the answer, so the picker can
			// switch to another run without asking for the window again.
			rep.Runs = base.Runs
			rep.Run = runSel
		}

		fillSpecs(rep.Bots, src.Specs, specs, &mu, nowFn())
		writeJSON(w, rep)
	}
}

// window reads the half-open [since, until) the request asks for: one UTC
// day, one local night (night with the history routes' tz and rollover), or
// an explicit since (until defaulting to now+1). A night with an unknown
// zone falls back to that UTC day, as the history list does.
func window(q map[string][]string, now time.Time) (since, until int64, fallback bool, msg string) {
	get := func(name string) string {
		if v := q[name]; len(v) > 0 {
			return v[0]
		}
		return ""
	}
	day, night, sinceS, untilS := get("day"), get("night"), get("since"), get("until")
	if night != "" {
		if day != "" || sinceS != "" || untilS != "" {
			return 0, 0, false, "night and a day window together"
		}
		loc, rollover, paramErr, fb := history.NightParams(get)
		if paramErr != "" {
			return 0, 0, false, paramErr
		}
		if loc == nil {
			return 0, 0, false, "bad night"
		}
		if fb {
			loc, rollover = time.UTC, 0
		}
		start, end, ok := history.NightWindow(night, loc, rollover)
		if !ok {
			return 0, 0, false, "bad night"
		}
		return start, end, fb, ""
	}
	if day != "" && sinceS != "" {
		return 0, 0, false, "day and since together"
	}
	if day != "" {
		t, err := time.ParseInLocation("2006-01-02", day, time.UTC)
		if err != nil || t.Format("2006-01-02") != day {
			return 0, 0, false, "bad day"
		}
		return t.Unix(), t.Unix() + 86400, false, ""
	}
	if sinceS == "" {
		return 0, 0, false, "day or since is required"
	}
	since, err := strconv.ParseInt(sinceS, 10, 64)
	if err != nil || since <= 0 {
		return 0, 0, false, "bad since"
	}
	until = now.Unix() + 1
	if untilS != "" {
		until, err = strconv.ParseInt(untilS, 10, 64)
		if err != nil {
			return 0, 0, false, "bad until"
		}
	}
	if until <= since {
		return 0, 0, false, "until must be after since"
	}
	return since, until, false, ""
}

// candidate is one stored fight before its detail is read. The lists carry
// only summaries, so the window is cut to the newest maxFights before any
// fight body is read (a day of history can hold more than the view keeps).
type candidate struct {
	key     string
	start   int64
	session uint64
	row     history.Row // the history index row of a fight the store no longer holds
	mem     bool
}

// collect reads the window's fights: memory first (its warnings are the ones
// the store judged with), then the history's fights the store no longer
// holds, re-judged with the current rules. The newest maxFights are kept.
func collect(src Sources, commander string, since, until int64, rules []combat.Rule, maxFights int) ([]Input, bool) {
	seen := map[string]bool{}
	var cands []candidate
	if src.Mem != nil {
		for _, row := range src.Mem.List(commander) {
			if row.Live || row.Start < since || row.Start >= until || seen[row.Key] {
				continue
			}
			seen[row.Key] = true
			cands = append(cands, candidate{key: row.Key, start: row.Start, session: row.Session, mem: true})
		}
	}
	if src.Hist != nil {
		for _, row := range src.Hist.ListAll(commander) {
			if seen[row.Key] || row.Start < since || row.Start >= until {
				continue
			}
			seen[row.Key] = true
			cands = append(cands, candidate{key: row.Key, start: row.Start, session: row.Session, row: row})
		}
	}
	sort.SliceStable(cands, func(i, j int) bool {
		if cands[i].start != cands[j].start {
			return cands[i].start < cands[j].start
		}
		return cands[i].key < cands[j].key
	})
	truncated := false
	if maxFights > 0 && len(cands) > maxFights {
		cands = cands[len(cands)-maxFights:]
		truncated = true
	}
	out := make([]Input, 0, len(cands))
	for _, c := range cands {
		if c.mem {
			f, warnings, ok := src.Mem.GetKey(c.key)
			if !ok {
				continue
			}
			out = append(out, Input{Key: c.key, Day: utcDay(c.start), Session: c.session, Fight: f, Warnings: warnings})
			continue
		}
		f, got, ok := src.Hist.Fight(c.key)
		if !ok {
			continue
		}
		start := c.start
		if start == 0 {
			start = got.Start
		}
		out = append(out, Input{
			Key: c.key, Day: utcDay(start), Session: c.session, Fight: f, Warnings: combat.Judge(f, rules),
		})
	}
	return out, truncated
}

func utcDay(unix int64) string {
	return time.Unix(unix, 0).UTC().Format("2006-01-02")
}

// fillSpecs names the listed bots' specs, asking the armory once per guid per
// specTTL and remembering an empty answer too (a bot with no talents).
func fillSpecs(bots []BotRow, specsFor func([]uint32) map[uint32]string, cache map[uint32]specEntry, mu *sync.Mutex, now time.Time) {
	if len(bots) == 0 || specsFor == nil {
		return
	}
	var missing []uint32
	mu.Lock()
	for _, b := range bots {
		e, ok := cache[b.GUID]
		if !ok || now.Sub(e.at) > specTTL {
			missing = append(missing, b.GUID)
		}
	}
	mu.Unlock()
	if len(missing) > 0 {
		got := specsFor(missing)
		mu.Lock()
		for _, guid := range missing {
			cache[guid] = specEntry{spec: got[guid], at: now}
		}
		mu.Unlock()
	}
	mu.Lock()
	defer mu.Unlock()
	for i := range bots {
		bots[i].Spec = cache[bots[i].GUID].spec
	}
}

func writeJSON(w http.ResponseWriter, payload interface{}) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(payload)
}

func writeErr(w http.ResponseWriter, msg string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusBadRequest)
	_ = json.NewEncoder(w).Encode(struct {
		Error string `json:"error"`
	}{msg})
}
