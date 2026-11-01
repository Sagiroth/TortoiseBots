package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"

	"tortoise-observability/web"
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

// TestBossTacticsRouteWired guards the coverage route: it must stay behind
// the GM session and read the world database the plans already opened.
func TestBossTacticsRouteWired(t *testing.T) {
	src, err := os.ReadFile("main.go")
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{
		"GET /api/v1/boss/tactics",
		"requireAuth(bosstactics.Handler(",
		"bosstactics.NewSQLSource(worldDB)",
	} {
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

// TestTrashDataRouteServed guards the trash table's route: the page asks for /data/trash_abilities.json, so the
// file must be in the binary (web.FS embeds web/data) and come back as the JSON the trash module reads.
func TestTrashDataRouteServed(t *testing.T) {
	src, err := os.ReadFile("main.go")
	if err != nil {
		t.Fatal(err)
	}
	if want := `mux.Handle("/data/", noCache(http.FileServer(http.FS(web.FS))))`; !strings.Contains(string(src), want) {
		t.Fatalf("main.go does not serve the embedded data folder: %q", want)
	}
	// noCache is a closure inside main(); the file server over the same embedded FS is what it wraps.
	srv := httptest.NewServer(http.FileServer(http.FS(web.FS)))
	defer srv.Close()
	resp, err := http.Get(srv.URL + "/data/trash_abilities.json")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("GET /data/trash_abilities.json = %d", resp.StatusCode)
	}
	var doc struct {
		Maps []struct {
			Key   string `json:"key"`
			MapID int    `json:"mapId"`
		} `json:"maps"`
		Mobs map[string]struct {
			Entry     int      `json:"entry"`
			Danger    int      `json:"danger"`
			Roles     []string `json:"roles"`
			Abilities []struct {
				SID int `json:"sid"`
			} `json:"abilities"`
		} `json:"mobs"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&doc); err != nil {
		t.Fatalf("the served trash table is no JSON: %v", err)
	}
	if len(doc.Mobs) == 0 || len(doc.Maps) == 0 {
		t.Fatalf("the served trash table is empty: %d mobs, %d maps", len(doc.Mobs), len(doc.Maps))
	}
	for key, mob := range doc.Mobs {
		if mob.Entry == 0 || mob.Danger < 1 || len(mob.Roles) == 0 || len(mob.Abilities) == 0 {
			t.Errorf("mob %s is incomplete: %+v", key, mob)
		}
	}
}

// TestFightsListQueryWired guards the live fights list's optional paging and filters (combat.ListQuery): parsed from the
// request, a bad param answered 400, applied to the store's list, and a cut page marked with X-More.
func TestFightsListQueryWired(t *testing.T) {
	src, err := os.ReadFile("main.go")
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{
		"lq, err := combat.ParseListQuery(r.URL.Query())",
		"rows, more := lq.Apply(combatStore.List(r.URL.Query().Get(\"commander\")))",
		"w.Header().Set(\"X-More\", \"1\")",
	} {
		if !strings.Contains(string(src), want) {
			t.Errorf("main.go does not contain %q", want)
		}
	}
}
