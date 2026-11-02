package main

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/history"
)

// TestRunTotalsRouteWired drives the run-totals handler over HTTP: missing
// params are a 400, memory and history fights de-duplicate by key, only
// the asked session's fights are summed, since stays an extra lower bound
// (unix ms), and the cap answers X-More.
func TestRunTotalsRouteWired(t *testing.T) {
	mk := func(id string, start int64, dmg uint64) combat.Fight {
		f := combat.Fight{ID: id, Commander: "G", Start: start, DurMS: 30000, Outcome: "kill"}
		f.Bots = []combat.FightBot{{Name: "Pew", GUID: 3, Class: "mage", Role: "dps", Sub: 0, Dmg: dmg}}
		return f
	}
	newMem := func() *combat.Store {
		st := combat.NewStore(nil)
		if !st.Add(1, mk("a", 1000, 100)) || !st.Add(1, mk("b", 2000, 200)) {
			t.Fatal("Add refused")
		}
		return st
	}
	newHist := func(t *testing.T, fights ...combat.Fight) *history.Store {
		t.Helper()
		now := time.Date(2026, 10, 28, 12, 0, 0, 0, time.UTC)
		clk := now
		s, err := history.Open(history.Config{Dir: t.TempDir(), Now: func() time.Time { return clk }},
			func(context.Context, string, combat.Fight) int { return 0 }, nil)
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { s.Close(context.Background()) })
		for _, f := range fights {
			if !s.Enqueue(history.Item{Session: 1, Fight: f, At: now}) {
				t.Fatal("Enqueue refused")
			}
		}
		clk = now.Add(7 * time.Second)
		s.Flush()
		return s
	}
	do := func(h http.HandlerFunc, target string) (int, combat.RunTotals, string, string) {
		rec := httptest.NewRecorder()
		h(rec, httptest.NewRequest(http.MethodGet, target, nil))
		var got combat.RunTotals
		_ = json.Unmarshal(rec.Body.Bytes(), &got)
		return rec.Code, got, rec.Header().Get("X-More"), rec.Body.String()
	}
	for _, target := range []string{
		"/api/v1/combat/run-totals",
		"/api/v1/combat/run-totals?commander=G",
		"/api/v1/combat/run-totals?session=1",
		"/api/v1/combat/run-totals?commander=G&session=nope",
		"/api/v1/combat/run-totals?commander=G&session=1&since=nope",
	} {
		if code, _, _, body := do(runTotalsHandler(newMem(), nil), target); code != 400 {
			t.Errorf("%s: status = %d, want 400 (%s)", target, code, body)
		}
	}
	runTotalsLive = &runTotalsCache{}
	code, got, more, body := do(runTotalsHandler(newMem(), nil), "/api/v1/combat/run-totals?commander=G&session=1&since=1000000")
	if code != 200 || more != "" || got.Fights != 2 || got.Since != 1000 || len(got.Bots) != 1 || got.Bots[0].Dmg != 300 {
		t.Errorf("all: %d %+v more=%q (%s)", code, got, more, body)
	}
	runTotalsLive = &runTotalsCache{}
	code, got, _, body = do(runTotalsHandler(newMem(), nil), "/api/v1/combat/run-totals?commander=G&session=1&since=2000000")
	if code != 200 || got.Fights != 1 || len(got.Bots) != 1 || got.Bots[0].Dmg != 200 {
		t.Errorf("since: %d %+v (%s)", code, got, body)
	}
	runTotalsLive = &runTotalsCache{}
	// The history holds the same fight "a": it is summed once, not twice.
	h := newHist(t, mk("a", 1000, 100))
	code, got, _, body = do(runTotalsHandler(newMem(), h), "/api/v1/combat/run-totals?commander=G&session=1&since=1000000")
	if code != 200 || got.Fights != 2 || got.Bots[0].Dmg != 300 {
		t.Errorf("dedup: %d %+v (%s)", code, got, body)
	}
	runTotalsLive = &runTotalsCache{}
	// The history holds a fight the memory dropped: it joins the sum.
	h = newHist(t, mk("old", 500, 50))
	code, got, _, body = do(runTotalsHandler(newMem(), h), "/api/v1/combat/run-totals?commander=G&session=1&since=400000")
	if code != 200 || got.Fights != 3 || got.Bots[0].Dmg != 350 {
		t.Errorf("history: %d %+v (%s)", code, got, body)
	}
	// A history fight started before since stays out.
	runTotalsLive = &runTotalsCache{}
	code, got, _, body = do(runTotalsHandler(newMem(), h), "/api/v1/combat/run-totals?commander=G&session=1&since=1000000")
	if code != 200 || got.Fights != 2 || got.Bots[0].Dmg != 300 {
		t.Errorf("history since: %d %+v (%s)", code, got, body)
	}
}

// TestRunTotalsRouteCap cuts past 500 fights and reports X-More.
func TestRunTotalsRouteCap(t *testing.T) {
	ms := combat.NewStore(nil)
	runTotalsLive = &runTotalsCache{}
	for i := range int64(5) {
		f := combat.Fight{ID: string(rune('a' + i)), Commander: "G", Start: 1000 + i, DurMS: 10000, Outcome: "kill"}
		if !ms.Add(1, f) {
			t.Fatal("Add refused")
		}
	}
	rec := httptest.NewRecorder()
	runTotalsHandlerCap(ms, nil, 2)(rec, httptest.NewRequest(http.MethodGet, "/api/v1/combat/run-totals?commander=G&session=1&since=1000000", nil))
	if rec.Code != 200 || rec.Header().Get("X-More") != "1" {
		t.Fatalf("cap: %d more=%q (%s)", rec.Code, rec.Header().Get("X-More"), rec.Body.String())
	}
	var got combat.RunTotals
	if err := json.Unmarshal(rec.Body.Bytes(), &got); err != nil || got.Fights != 2 || got.Since != 1000 {
		t.Fatalf("cap body: %+v err=%v (%s)", got, err, rec.Body.String())
	}
}

// TestRunTotalsRouteSessionScope sums only the asked session: two sessions
// of one commander, and a later day's fight of the same commander, stay out
// of each other's totals.
func TestRunTotalsRouteSessionScope(t *testing.T) {
	mk := func(id string, session uint64, start int64, dmg uint64) combat.Fight {
		f := combat.Fight{ID: id, Commander: "G", Start: start, DurMS: 30000, Outcome: "kill"}
		f.Bots = []combat.FightBot{{Name: "Pew", GUID: 3, Class: "mage", Role: "dps", Sub: 0, Dmg: dmg}}
		return f
	}
	newHist := func(t *testing.T, items ...history.Item) *history.Store {
		t.Helper()
		now := time.Date(2026, 10, 28, 12, 0, 0, 0, time.UTC)
		clk := now
		s, err := history.Open(history.Config{Dir: t.TempDir(), Now: func() time.Time { return clk }},
			func(context.Context, string, combat.Fight) int { return 0 }, nil)
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { s.Close(context.Background()) })
		for _, it := range items {
			if !s.Enqueue(it) {
				t.Fatal("Enqueue refused")
			}
		}
		clk = now.Add(7 * time.Second)
		s.Flush()
		return s
	}
	do := func(h http.HandlerFunc, target string) (int, combat.RunTotals, string) {
		rec := httptest.NewRecorder()
		h(rec, httptest.NewRequest(http.MethodGet, target, nil))
		var got combat.RunTotals
		_ = json.Unmarshal(rec.Body.Bytes(), &got)
		return rec.Code, got, rec.Body.String()
	}
	at := time.Date(2026, 10, 28, 12, 0, 0, 0, time.UTC)
	runTotalsLive = &runTotalsCache{}
	ms := combat.NewStore(nil)
	if !ms.Add(7, mk("a", 7, 1000, 100)) || !ms.Add(8, mk("b", 8, 2000, 200)) {
		t.Fatal("Add refused")
	}
	h := newHist(t,
		history.Item{Session: 7, Fight: mk("a", 7, 1000, 100), At: at},
		history.Item{Session: 8, Fight: mk("b", 8, 2000, 200), At: at},
		history.Item{Session: 8, Fight: mk("c", 8, 86400+5000, 400), At: at},
	)
	code, got, body := do(runTotalsHandler(ms, h), "/api/v1/combat/run-totals?commander=G&session=7&since=0")
	if code != 200 || got.Fights != 1 || len(got.Bots) != 1 || got.Bots[0].Dmg != 100 {
		t.Fatalf("session 7: %d %+v (%s)", code, got, body)
	}
	code, got, body = do(runTotalsHandler(ms, h), "/api/v1/combat/run-totals?commander=G&session=8&since=0")
	if code != 200 || got.Fights != 2 || len(got.Bots) != 1 || got.Bots[0].Dmg != 600 {
		t.Fatalf("session 8: %d %+v (%s)", code, got, body)
	}
	// A missing session is a 400, like a missing commander.
	if code, _, body := do(runTotalsHandler(ms, h), "/api/v1/combat/run-totals?commander=G&since=0"); code != 400 {
		t.Fatalf("no session: status = %d, want 400 (%s)", code, body)
	}
}

// TestRunTotalsRouteOpensOnlyMatchingRows fills the history with other
// sessions' fights and asks one session: the handler opens only the matching
// index rows, newest first, and stops at the cap.
func TestRunTotalsRouteOpensOnlyMatchingRows(t *testing.T) {
	mk := func(id string, session uint64, start int64) combat.Fight {
		f := combat.Fight{ID: id, Commander: "G", Start: start, DurMS: 10000, Outcome: "kill"}
		f.Bots = []combat.FightBot{{Name: "Pew", GUID: 3, Class: "mage", Role: "dps", Sub: 0, Dmg: 1}}
		return f
	}
	mem := combat.NewStore(nil)
	runTotalsLive = &runTotalsCache{}
	fake := &fakeRunHistory{fights: map[string]combat.Fight{}}
	var rows []history.Row
	for i := range 1000 {
		sess := uint64(99)
		id := "other"
		start := int64(1000 + i)
		if i < 3 {
			sess = 7007
			id = string(rune('a' + i))
			start = int64(9000 - i)
		}
		key := combat.Key("G", sess, id+"-"+string(rune('0'+i%10))+string(rune('a'+i/10%26))+string(rune('a'+i/100%26)))
		if i < 3 {
			key = combat.Key("G", sess, id)
		}
		rows = append(rows, history.Row{Key: key, Commander: "G", Session: sess, ID: id, Start: start})
		fake.fights[key] = mk(id, sess, start)
	}
	_ = mem
	h := &fakeRunHistory{rows: rows, fights: fake.fights}
	rec := httptest.NewRecorder()
	runTotalsLive = &runTotalsCache{}
	runTotalsHandlerCap(nil, h, 1000)(rec, httptest.NewRequest(http.MethodGet, "/api/v1/combat/run-totals?commander=G&session=7007", nil))
	if rec.Code != 200 || rec.Header().Get("X-More") != "" {
		t.Fatalf("all three: %d more=%q (%s)", rec.Code, rec.Header().Get("X-More"), rec.Body.String())
	}
	var all combat.RunTotals
	if err := json.Unmarshal(rec.Body.Bytes(), &all); err != nil || all.Fights != 3 {
		t.Fatalf("all three body: %+v err=%v (%s)", all, err, rec.Body.String())
	}
	if len(h.opened) != 3 {
		t.Fatalf("opened %d fights, want the 3 matching rows: %v", len(h.opened), h.opened)
	}
	rec = httptest.NewRecorder()
	h.opened, h.openOrder = nil, nil
	runTotalsLive = &runTotalsCache{}
	runTotalsHandlerCap(nil, h, 2)(rec, httptest.NewRequest(http.MethodGet, "/api/v1/combat/run-totals?commander=G&session=7007", nil))
	if rec.Code != 200 || rec.Header().Get("X-More") != "1" {
		t.Fatalf("cut: %d more=%q (%s)", rec.Code, rec.Header().Get("X-More"), rec.Body.String())
	}
	var got combat.RunTotals
	if err := json.Unmarshal(rec.Body.Bytes(), &got); err != nil || got.Fights != 2 {
		t.Fatalf("body: %+v err=%v (%s)", got, err, rec.Body.String())
	}
	// The third matching row is opened to learn the cut, then the oldest is
	// dropped newest-first: only two are summed, with X-More.
	if len(h.opened) != 3 {
		t.Fatalf("opened %d fights, want the 3 matching rows: %v", len(h.opened), h.opened)
	}
	for _, key := range h.opened {
		row, ok := h.byKey(key)
		if !ok || row.Session != 7007 {
			t.Fatalf("opened a row outside session 7007: %q", key)
		}
	}
	if h.openOrder[0] != rows[0].Key || h.openOrder[1] != rows[1].Key || h.openOrder[2] != rows[2].Key {
		t.Fatalf("opened newest first: %v", h.openOrder)
	}
}

type fakeRunHistory struct {
	rows      []history.Row
	fights    map[string]combat.Fight
	opened    []string
	openOrder []string
}

func (f *fakeRunHistory) ListAll(commander string) []history.Row {
	var out []history.Row
	for _, r := range f.rows {
		if r.Commander == commander {
			out = append(out, r)
		}
	}
	return out
}

func (f *fakeRunHistory) Fight(key string) (combat.Fight, history.Row, bool) {
	f.opened = append(f.opened, key)
	f.openOrder = append(f.openOrder, key)
	for _, r := range f.rows {
		if r.Key == key {
			return f.fights[key], r, true
		}
	}
	return combat.Fight{}, history.Row{}, false
}

func (f *fakeRunHistory) byKey(key string) (history.Row, bool) {
	for _, r := range f.rows {
		if r.Key == key {
			return r, true
		}
	}
	return history.Row{}, false
}
