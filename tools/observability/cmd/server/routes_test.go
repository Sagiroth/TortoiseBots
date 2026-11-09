package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"strings"
	"testing"
	"time"

	"tortoise-observability/internal/auth"
	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/mechanics"
	"tortoise-observability/internal/underperf"
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
	if !strings.Contains(string(src), "combatStore.AnnotateRaids(&view)") {
		t.Error("the raids route does not join the live rows' boss names")
	}
	for _, want := range []string{"GET /api/v1/commanders/track", "state.ParseTrackWindow(", "store.TrackWindow(", "state.TrackJSONX("} {
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
		"combatFightDetailHandler(combatStore, historyStore, store)",
		"historyStore.SetMeters(",
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

// TestFightsListQueryWired drives the live fights list's handler: no params answer the old list, limit cuts it with X-More,
// the before cursor pages on, the filters filter, and a bad param is a 400.
func TestFightsListQueryWired(t *testing.T) {
	src, err := os.ReadFile("main.go")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(src), `mux.HandleFunc("/api/v1/combat/fights", requireAuth(combatFightsHandler(combatStore, historyStore)))`) {
		t.Fatal("the route does not serve combatFightsHandler behind requireAuth")
	}
	st := combat.NewStore(nil)
	add := func(id string, start int64, deaths int, boss, outcome string) {
		f := combat.Fight{ID: id, Commander: "G", Start: start, DurMS: 30000, Outcome: outcome, BossName: boss}
		for i := 0; i < deaths; i++ {
			f.Deaths = append(f.Deaths, combat.FightDeath{Name: "T"})
		}
		if !st.Add(1, f) {
			t.Fatalf("Add %s refused", id)
		}
	}
	add("a", 100, 0, "", "end")
	add("b", 200, 2, "", "end")
	add("c", 300, 0, "Onyxia", "kill")
	add("d", 400, 5, "", "wipe")
	h := combatFightsHandler(st, nil)
	do := func(q string) (int, []combat.FightSummary, string, string) {
		rec := httptest.NewRecorder()
		h(rec, httptest.NewRequest(http.MethodGet, "/api/v1/combat/fights?commander=G"+q, nil))
		var rows []combat.FightSummary
		_ = json.Unmarshal(rec.Body.Bytes(), &rows)
		return rec.Code, rows, rec.Header().Get("X-More"), rec.Body.String()
	}
	ids := func(rows []combat.FightSummary) string {
		out := []string{}
		for _, r := range rows {
			out = append(out, r.ID)
		}
		return strings.Join(out, " ")
	}
	cases := []struct{ q, want, more string }{
		{"", "d c b a", ""},
		{"&limit=2", "d c", "1"},
		{"&before=300", "b a", ""},
		{"&before=300&limit=1", "b", "1"},
		{"&since=200&limit=10", "d c b", ""},
		{"&boss=1", "c", ""},
		{"&deaths=1", "d b", ""},
		{"&outcome=wipe", "d", ""},
	}
	for _, c := range cases {
		code, rows, more, body := do(c.q)
		if code != 200 || ids(rows) != c.want || more != c.more {
			t.Errorf("%q: %d %q more=%q (%s), want %q more=%q", c.q, code, ids(rows), more, body, c.want, c.more)
		}
	}
	if code, _, _, body := do("&limit=0"); code != 400 || !strings.Contains(body, "bad limit") {
		t.Errorf("limit=0: %d %s", code, body)
	}
}

// TestCombatLiveRoute: the catch-up route answers 404 without a live row, 200 with the cursor and the held events after one
// COMBAT_EVENTS datagram, and 400 for a bad cursor or name; it is registered behind requireAuth.
func TestCombatLiveRoute(t *testing.T) {
	src, err := os.ReadFile("main.go")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(src), `mux.HandleFunc("GET /api/v1/combat/live", requireAuth(combatLiveHandler(combatStore)))`) {
		t.Fatal("the route does not serve combatLiveHandler behind requireAuth")
	}
	st := combat.NewStore(nil)
	h := combatLiveHandler(st)
	do := func(target string) (int, string) {
		rec := httptest.NewRecorder()
		h(rec, httptest.NewRequest(http.MethodGet, target, nil))
		return rec.Code, rec.Body.String()
	}
	if code, body := do("/api/v1/combat/live?commander=Gbmage"); code != 404 || !strings.Contains(body, "no live fight") {
		t.Fatalf("no live row: %d %s", code, body)
	}
	now := time.Now()
	dg := combat.EventsDatagram{
		FMT: 1, Session: 7, Seq: 1, Commander: "Gbmage", Group: 5, Map: 409,
		Fight: &combat.LiveHeader{ID: "5-1", Start: now.Unix(), StartMs: now.UnixMilli(), Boss: 11502, BossName: "Magmadar", FightMs: 1000},
		Events: []json.RawMessage{
			json.RawMessage(`{"k":"start","t":0}`),
			json.RawMessage(`{"k":"cast","t":500,"n":"Magmadar","s":"Panic"}`),
		},
	}
	if _, _, ok := st.ApplyEvents(dg, now); !ok {
		t.Fatal("ApplyEvents refused the datagram")
	}
	code, body := do("/api/v1/combat/live?commander=Gbmage&since=1")
	if code != 200 {
		t.Fatalf("live row: %d %s", code, body)
	}
	var got struct {
		Cur    uint64 `json:"cur"`
		From   uint64 `json:"from"`
		Key    string `json:"key"`
		Events []struct {
			I uint64 `json:"i"`
		} `json:"events"`
	}
	if err := json.Unmarshal([]byte(body), &got); err != nil {
		t.Fatal(err)
	}
	if got.Cur != 2 || got.From != 1 || len(got.Events) != 1 || got.Events[0].I != 1 || got.Key == "" {
		t.Fatalf("delta: %s", body)
	}
	// A cursor that belongs to another fight (key=) answers from 0.
	code, body = do("/api/v1/combat/live?commander=Gbmage&since=1&key=" + url.QueryEscape("Gbmage\x007\x005-0"))
	if code != 200 || !strings.Contains(body, `"from":0`) {
		t.Fatalf("another fight's key: %d %s", code, body)
	}
	if code, body := do("/api/v1/combat/live?commander=Gbmage&since=x"); code != 400 || !strings.Contains(body, "bad since") {
		t.Fatalf("since=x: %d %s", code, body)
	}
	if code, _ := do("/api/v1/combat/live"); code != 400 {
		t.Fatalf("no commander: %d", code)
	}
	if code, _ := do("/api/v1/combat/live?commander=a&commander=b"); code != 400 {
		t.Fatalf("two commanders: %d", code)
	}
}

// TestMechanicsRouteIsBehindTheSession guards the boss mechanics route: it is registered behind requireAuth (the source pin, as for the
// other inline routes) and the gate every requireAuth route shares answers a request with no session 401 on an API path, so the
// handler is never reached.
func TestMechanicsRouteIsBehindTheSession(t *testing.T) {
	src, err := os.ReadFile("main.go")
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{
		`mux.HandleFunc("GET /api/v1/combat/mechanics", requireAuth(mechanics.Handler(`,
		"return sessionGate(authService, *devNoAuth, next)",
	} {
		if !strings.Contains(string(src), want) {
			t.Errorf("main.go does not contain %q", want)
		}
	}
	svc, err := auth.NewService(auth.Config{SecretKey: "mechanics-test-secret"})
	if err != nil {
		t.Fatal(err)
	}
	reached := false
	h := sessionGate(svc, false, func(w http.ResponseWriter, r *http.Request, _ *auth.SessionData) {
		reached = true
		mechanics.Handler(underperf.Sources{})(w, r)
	})
	for _, target := range []string{"/api/v1/combat/mechanics?commander=T&day=2026-11-01", "/api/v1/combat/mechanics"} {
		rec := httptest.NewRecorder()
		h(rec, httptest.NewRequest(http.MethodGet, target, nil))
		if rec.Code != http.StatusUnauthorized || !strings.Contains(rec.Body.String(), "Unauthorized") {
			t.Errorf("%s with no session: %d %s, want 401", target, rec.Code, rec.Body.String())
		}
	}
	if reached {
		t.Fatal("the handler ran without a session")
	}
	// a page path is sent to the login instead
	rec := httptest.NewRecorder()
	h(rec, httptest.NewRequest(http.MethodGet, "/dashboard", nil))
	if rec.Code != http.StatusFound || rec.Header().Get("Location") != "/login" {
		t.Fatalf("a page with no session: %d %q", rec.Code, rec.Header().Get("Location"))
	}
	// the dev flag lets a request through
	ok := httptest.NewRecorder()
	sessionGate(svc, true, func(w http.ResponseWriter, r *http.Request, _ *auth.SessionData) { w.WriteHeader(http.StatusNoContent) })(ok, httptest.NewRequest(http.MethodGet, "/api/v1/x", nil))
	if ok.Code != http.StatusNoContent {
		t.Fatalf("dev no-auth: %d", ok.Code)
	}
}
