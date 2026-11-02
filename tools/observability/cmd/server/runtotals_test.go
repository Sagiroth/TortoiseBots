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
// fights started at or after the since (unix ms) are summed, and the cap
// answers X-More.
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
		"/api/v1/combat/run-totals?since=1000000",
		"/api/v1/combat/run-totals?commander=G&since=nope",
	} {
		if code, _, _, body := do(runTotalsHandler(newMem(), nil), target); code != 400 {
			t.Errorf("%s: status = %d, want 400 (%s)", target, code, body)
		}
	}
	code, got, more, body := do(runTotalsHandler(newMem(), nil), "/api/v1/combat/run-totals?commander=G&since=1000000")
	if code != 200 || more != "" || got.Fights != 2 || got.Since != 1000 || len(got.Bots) != 1 || got.Bots[0].Dmg != 300 {
		t.Errorf("all: %d %+v more=%q (%s)", code, got, more, body)
	}
	code, got, _, body = do(runTotalsHandler(newMem(), nil), "/api/v1/combat/run-totals?commander=G&since=2000000")
	if code != 200 || got.Fights != 1 || len(got.Bots) != 1 || got.Bots[0].Dmg != 200 {
		t.Errorf("since: %d %+v (%s)", code, got, body)
	}
	// The history holds the same fight "a": it is summed once, not twice.
	h := newHist(t, mk("a", 1000, 100))
	code, got, _, body = do(runTotalsHandler(newMem(), h), "/api/v1/combat/run-totals?commander=G&since=1000000")
	if code != 200 || got.Fights != 2 || got.Bots[0].Dmg != 300 {
		t.Errorf("dedup: %d %+v (%s)", code, got, body)
	}
	// The history holds a fight the memory dropped: it joins the sum.
	h = newHist(t, mk("old", 500, 50))
	code, got, _, body = do(runTotalsHandler(newMem(), h), "/api/v1/combat/run-totals?commander=G&since=400000")
	if code != 200 || got.Fights != 3 || got.Bots[0].Dmg != 350 {
		t.Errorf("history: %d %+v (%s)", code, got, body)
	}
	// A history fight started before since stays out.
	code, got, _, body = do(runTotalsHandler(newMem(), h), "/api/v1/combat/run-totals?commander=G&since=1000000")
	if code != 200 || got.Fights != 2 || got.Bots[0].Dmg != 300 {
		t.Errorf("history since: %d %+v (%s)", code, got, body)
	}
}

// TestRunTotalsRouteCap cuts past 500 fights and reports X-More.
func TestRunTotalsRouteCap(t *testing.T) {
	ms := combat.NewStore(nil)
	for i := int64(0); i < 5; i++ {
		f := combat.Fight{ID: string(rune('a' + i)), Commander: "G", Start: 1000 + i, DurMS: 10000, Outcome: "kill"}
		if !ms.Add(1, f) {
			t.Fatal("Add refused")
		}
	}
	rec := httptest.NewRecorder()
	runTotalsHandlerCap(ms, nil, 2)(rec, httptest.NewRequest(http.MethodGet, "/api/v1/combat/run-totals?commander=G&since=1000000", nil))
	if rec.Code != 200 || rec.Header().Get("X-More") != "1" {
		t.Fatalf("cap: %d more=%q (%s)", rec.Code, rec.Header().Get("X-More"), rec.Body.String())
	}
	var got combat.RunTotals
	if err := json.Unmarshal(rec.Body.Bytes(), &got); err != nil || got.Fights != 2 || got.Since != 1000 {
		t.Fatalf("cap body: %+v err=%v (%s)", got, err, rec.Body.String())
	}
}
