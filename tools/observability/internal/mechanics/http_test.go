package mechanics

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/history"
	"tortoise-observability/internal/underperf"
)

type fakeMem struct {
	rows   []combat.FightSummary
	fights map[string]combat.Fight
}

func (m *fakeMem) List(string) []combat.FightSummary { return m.rows }
func (m *fakeMem) GetKey(key string) (combat.Fight, []combat.Warning, bool) {
	f, ok := m.fights[key]
	return f, nil, ok
}

type fakeHist struct {
	rows   []history.Row
	fights map[string]combat.Fight
}

func (h *fakeHist) ListAll(string) []history.Row { return h.rows }
func (h *fakeHist) Fight(key string) (combat.Fight, history.Row, bool) {
	f, ok := h.fights[key]
	for _, r := range h.rows {
		if r.Key == key {
			return f, r, ok
		}
	}
	return f, history.Row{}, ok
}

func get(h http.Handler, target string) *httptest.ResponseRecorder {
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, httptest.NewRequest(http.MethodGet, target, nil))
	return rec
}

func TestHandlerWindowAndShape(t *testing.T) {
	day := time.Date(2026, 11, 1, 0, 0, 0, 0, time.UTC).Unix()
	inDay, nextDay := hakkar(), hakkar()
	inDay.Start, nextDay.Start = day+3600, day+86400+3600
	inDay.ID, nextDay.ID = "f1", "f2"
	k1, k2 := combat.Key("T", 7, "f1"), combat.Key("T", 7, "f2")
	mem := &fakeMem{
		rows: []combat.FightSummary{
			{ID: "f1", Key: k1, Session: 7, Start: inDay.Start, Dur: inDay.DurMS},
			{ID: "f2", Key: k2, Session: 7, Start: nextDay.Start, Dur: nextDay.DurMS},
			{ID: "live", Key: "live", Live: true, Start: day + 10},
		},
		fights: map[string]combat.Fight{k1: inDay, k2: nextDay},
	}
	// the same key on disk too (counts once), and a fight only the history holds
	k3 := combat.Key("T", 6, "f3")
	old := hakkar()
	old.ID, old.Start = "f3", day+7200
	hist := &fakeHist{
		rows:   []history.Row{{Key: k1, Commander: "T", Session: 7, Start: inDay.Start}, {Key: k3, Commander: "T", Session: 6, Start: old.Start}},
		fights: map[string]combat.Fight{k1: inDay, k3: old},
	}
	h := Handler(underperf.Sources{Mem: mem, Hist: hist})
	rec := get(h, "/api/v1/combat/mechanics?commander=T&day=2026-11-01")
	if rec.Code != http.StatusOK || !strings.HasPrefix(rec.Header().Get("Content-Type"), "application/json") {
		t.Fatalf("status %d: %s", rec.Code, rec.Body.String())
	}
	var a Answer
	if err := json.Unmarshal(rec.Body.Bytes(), &a); err != nil {
		t.Fatal(err)
	}
	if a.Fights != 2 || len(a.Rows) != 1 || a.Rows[0].Fights != 2 || a.Rows[0].Boss != 14834 {
		t.Fatalf("the window holds two Hakkar fights (the key in both sources once, the next day's out): %+v", a)
	}
	if a.Window.Since != day || a.Window.Until != day+86400 {
		t.Fatalf("window: %+v", a.Window)
	}
	if w := a.Rows[0].Worst; w != k1 && w != k3 {
		t.Fatalf("worst %q is neither fight of the window", w)
	}
	var raw map[string]json.RawMessage
	_ = json.Unmarshal(rec.Body.Bytes(), &raw)
	for _, k := range []string{"rows", "fights", "window"} {
		if _, ok := raw[k]; !ok {
			t.Errorf("no %q in %s", k, rec.Body.String())
		}
	}
	// an empty window answers an empty list, not null
	if body := get(h, "/api/v1/combat/mechanics?commander=T&day=2030-01-01").Body.String(); !strings.Contains(body, `"rows":[]`) {
		t.Fatalf("empty window: %s", body)
	}
}

func TestHandlerBadParams(t *testing.T) {
	h := Handler(underperf.Sources{Mem: &fakeMem{}})
	for _, target := range []string{
		"/api/v1/combat/mechanics",
		"/api/v1/combat/mechanics?commander=T",
		"/api/v1/combat/mechanics?commander=T&day=2026-11-1",
		"/api/v1/combat/mechanics?commander=T&day=2026-11-01&since=100",
		"/api/v1/combat/mechanics?commander=T&since=100&until=100",
		"/api/v1/combat/mechanics?commander=T&night=2026-11-01&day=2026-11-01",
	} {
		rec := get(h, target)
		if rec.Code != http.StatusBadRequest || !strings.Contains(rec.Body.String(), `"error"`) {
			t.Errorf("%s: status %d body %s, want 400 with an error", target, rec.Code, rec.Body.String())
		}
	}
	// no history store at all (nil interface): memory only, no panic
	if rec := get(Handler(underperf.Sources{Mem: &fakeMem{}}), "/api/v1/combat/mechanics?commander=T&since=100"); rec.Code != http.StatusOK {
		t.Fatalf("since form: %d %s", rec.Code, rec.Body.String())
	}
}

// A night window is the history list's night (the page's zone and the 06:00 rollover), not a UTC day.
func TestHandlerNightWindow(t *testing.T) {
	mk := func(id string, at time.Time) (combat.FightSummary, combat.Fight) {
		f := hakkar()
		f.ID, f.Start = id, at.Unix()
		return combat.FightSummary{ID: id, Key: combat.Key("T", 1, id), Session: 1, Start: f.Start, Dur: f.DurMS}, f
	}
	mem := &fakeMem{fights: map[string]combat.Fight{}}
	for id, at := range map[string]time.Time{
		"early": time.Date(2026, 10, 24, 3, 59, 0, 0, time.UTC), // 05:59 CEST: the night of 23 Oct
		"late":  time.Date(2026, 10, 25, 0, 30, 0, 0, time.UTC), // 02:30 CEST: still the night of 24 Oct
		"next":  time.Date(2026, 10, 25, 5, 0, 0, 0, time.UTC),  // 06:00 CET: the night of 25 Oct
	} {
		sum, f := mk(id, at)
		mem.rows = append(mem.rows, sum)
		mem.fights[sum.Key] = f
	}
	h := Handler(underperf.Sources{Mem: mem})
	for night, want := range map[string]int{"2026-10-23": 1, "2026-10-24": 1, "2026-10-25": 1, "2026-10-26": 0} {
		rec := get(h, "/api/v1/combat/mechanics?commander=T&night="+night+"&tz=Europe%2FOslo&rollover=6")
		var a Answer
		if err := json.Unmarshal(rec.Body.Bytes(), &a); err != nil || rec.Code != http.StatusOK {
			t.Fatalf("%s: %d %s", night, rec.Code, rec.Body.String())
		}
		if a.Fights != want {
			t.Errorf("night %s: %d fights, want %d", night, a.Fights, want)
		}
	}
}
