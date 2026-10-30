package main

import (
	"os"
	"strings"
	"testing"
)

// TestTrackRouteWired guards the route, its parsing and its store call: the
// handler is an inline closure in main(), so the source is the only thing a
// test can see without a live daemon.
func TestTrackRouteWired(t *testing.T) {
	src, err := os.ReadFile("main.go")
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{"GET /api/v1/commanders/track", "state.ParseTrackWindow(", "store.TrackWindow("} {
		if !strings.Contains(string(src), want) {
			t.Errorf("main.go does not contain %q", want)
		}
	}
}

// TestHistoryRoutesWired guards the fight history's wiring: the three routes, the handlers behind them, the store opened from
// its flag and handed to the listener before Start, and the detail route's fallback to disk.
func TestHistoryRoutesWired(t *testing.T) {
	src, err := os.ReadFile("main.go")
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{
		"GET /api/v1/combat/history/days",
		"GET /api/v1/combat/history/commanders",
		"GET /api/v1/combat/history\"",
		"GET /api/v1/combat/packs",
		"requireAuth(history.DaysHandler(historyStore))",
		"requireAuth(history.CommandersHandler(historyStore))",
		"requireAuth(history.ListHandler(historyStore))",
		"requireAuth(history.PackHandler(historyStore))",
		"fight-history-dir",
		"TBDASH_FIGHT_HISTORY_DIR",
		"history.Open(history.Config{Dir: *fightHistoryDir}",
		"udpListener.SetHistory(historyStore)",
		"historyStore.Fight(",
		"historyStore.Lookup(",
	} {
		if !strings.Contains(string(src), want) {
			t.Errorf("main.go does not contain %q", want)
		}
	}
}
