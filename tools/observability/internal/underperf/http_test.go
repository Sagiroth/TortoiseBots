package underperf

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/history"
)

type fakeMem struct {
	rows   []combat.FightSummary
	fights map[string]combat.Fight
	warns  map[string][]combat.Warning
}

func (m *fakeMem) List(commander string) []combat.FightSummary { return m.rows }
func (m *fakeMem) GetKey(key string) (combat.Fight, []combat.Warning, bool) {
	f, ok := m.fights[key]
	return f, m.warns[key], ok
}

type fakeHist struct {
	rows   []history.Row
	fights map[string]combat.Fight
}

func (h *fakeHist) ListAll(commander string) []history.Row { return h.rows }
func (h *fakeHist) Fight(key string) (combat.Fight, history.Row, bool) {
	f, ok := h.fights[key]
	for _, r := range h.rows {
		if r.Key == key {
			return f, r, ok
		}
	}
	return f, history.Row{}, ok
}

func memRow(key string, f combat.Fight, session uint64) combat.FightSummary {
	return combat.FightSummary{ID: f.ID, Key: key, Session: session, Start: f.Start, Dur: f.DurMS}
}

func get(t *testing.T, h http.Handler, target string) *httptest.ResponseRecorder {
	t.Helper()
	req := httptest.NewRequest(http.MethodGet, target, nil)
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, req)
	return rec
}

func decodeAnswer(t *testing.T, rec *httptest.ResponseRecorder) Report {
	t.Helper()
	if rec.Code != http.StatusOK {
		t.Fatalf("status %d: %s", rec.Code, rec.Body.String())
	}
	var r Report
	if err := json.Unmarshal(rec.Body.Bytes(), &r); err != nil {
		t.Fatalf("decode: %v in %s", err, rec.Body.String())
	}
	return r
}

func dayOf(y int, m time.Month, d int) int64 {
	return time.Date(y, m, d, 0, 0, 0, 0, time.UTC).Unix()
}

func TestMemoryAndHistoryCountOnce(t *testing.T) {
	start := dayOf(2026, 11, 1) + 12*3600
	key := combat.Key("T", 7, "f1")
	f := combat.Fight{ID: "f1", Commander: "T", Start: start, DurMS: 30000, Outcome: "end",
		Bots: []combat.FightBot{
			tb(1, "A", "rogue", "dps", 0, 0, 1, 0, 0),
			tb(2, "B", "mage", "dps", 0, 0, 1, 0, 0),
			tb(3, "C", "hunter", "dps", 0, 0, 1, 0, 0),
		}}
	mem := &fakeMem{
		rows:   []combat.FightSummary{memRow(key, f, 7)},
		fights: map[string]combat.Fight{key: f},
		// The store judged this fight with no warnings; a re-judge would
		// raise dps_no_damage for all three.
		warns: map[string][]combat.Warning{key: nil},
	}
	hist := &fakeHist{
		rows:   []history.Row{{Key: key, Commander: "T", Session: 7, Start: start}},
		fights: map[string]combat.Fight{key: f},
	}
	rules := func() []combat.Rule { return rulesOf(t) }
	h := Handler(Sources{Mem: mem, Hist: hist, Rules: rules})
	r := decodeAnswer(t, get(t, h, "/api/v1/combat/underperformers?commander=T&day=2026-11-01"))
	if r.Fights != 1 {
		t.Fatalf("fights = %d, want 1 (the key in both sources counts once)", r.Fights)
	}
	for _, row := range r.Bots {
		for _, reason := range row.Reasons {
			if strings.HasPrefix(reason.ID, "rule:") {
				t.Errorf("%s has %q, but the memory store's warnings are the truth", row.Name, reason.ID)
			}
		}
	}
}

func TestBadParams(t *testing.T) {
	mem := &fakeMem{
		rows: []combat.FightSummary{memRow(combat.Key("T", 1, "f1"),
			combat.Fight{ID: "f1", Start: dayOf(2026, 11, 1) + 3600, DurMS: 30000}, 1)},
		fights: map[string]combat.Fight{combat.Key("T", 1, "f1"): {ID: "f1", Start: dayOf(2026, 11, 1) + 3600, DurMS: 30000}},
	}
	h := Handler(Sources{Mem: mem})
	cases := []string{
		"/api/v1/combat/underperformers",
		"/api/v1/combat/underperformers?commander=T&day=2026-11-1",
		"/api/v1/combat/underperformers?commander=T&day=2026-11-01&since=100",
		"/api/v1/combat/underperformers?commander=T&since=100&until=100",
		"/api/v1/combat/underperformers?commander=T&day=2026-11-01&run=5",
		"/api/v1/combat/underperformers?commander=T&day=2026-11-01&run=x",
	}
	for _, target := range cases {
		rec := get(t, h, target)
		if rec.Code != http.StatusBadRequest {
			t.Errorf("%s: status %d, want 400", target, rec.Code)
			continue
		}
		var body struct {
			Error string `json:"error"`
		}
		if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil || body.Error == "" {
			t.Errorf("%s: body %q, want a JSON error", target, rec.Body.String())
		}
	}
}

func TestNoHistory(t *testing.T) {
	start := dayOf(2026, 11, 1) + 12*3600
	key := combat.Key("T", 1, "f1")
	f := combat.Fight{ID: "f1", Start: start, DurMS: 30000, Bots: []combat.FightBot{tb(1, "A", "rogue", "dps", 100, 0, 1, 0, 0)}}
	mem := &fakeMem{
		rows:   []combat.FightSummary{memRow(key, f, 1)},
		fights: map[string]combat.Fight{key: f},
		warns:  map[string][]combat.Warning{key: {{Rule: "note", Level: "yellow", Bot: "A", Role: "dps", Text: "A note"}}},
	}
	r := decodeAnswer(t, get(t, Handler(Sources{Mem: mem}), "/api/v1/combat/underperformers?commander=T&day=2026-11-01"))
	if r.Fights != 1 || len(r.Bots) != 1 {
		t.Fatalf("fights = %d bots = %+v, want the memory fight only", r.Fights, r.Bots)
	}
}

func TestTruncated(t *testing.T) {
	base := dayOf(2026, 11, 2)
	mem := &fakeMem{fights: map[string]combat.Fight{}, warns: map[string][]combat.Warning{}}
	for i := range 501 {
		key := combat.Key("T", 1, "f"+itoa(i))
		f := combat.Fight{ID: "f" + itoa(i), Start: base + int64(i*60), DurMS: 30000,
			Bots: []combat.FightBot{tb(uint32(i+1), "B"+itoa(i), "rogue", "dps", 3000, 0, 10, 0, 0)}}
		mem.rows = append(mem.rows, memRow(key, f, 1))
		mem.fights[key] = f
		mem.warns[key] = nil
	}
	r := decodeAnswer(t, get(t, Handler(Sources{Mem: mem}), "/api/v1/combat/underperformers?commander=T&day=2026-11-02"))
	if r.Fights != 500 || !r.Truncated {
		t.Fatalf("fights = %d truncated = %v, want 500 and true", r.Fights, r.Truncated)
	}
	for _, ok := range r.OK {
		if ok.Name == "B0" {
			t.Errorf("B0 (the oldest fight) is in the answer; the newest 500 must be kept")
		}
	}
	found := false
	for _, ok := range r.OK {
		if ok.Name == "B500" {
			found = true
		}
	}
	if !found {
		t.Errorf("B500 (the newest fight) is missing")
	}
}

func TestSpecs(t *testing.T) {
	start := dayOf(2026, 11, 1) + 12*3600
	key := combat.Key("T", 1, "f1")
	f := combat.Fight{ID: "f1", Start: start, DurMS: 30000, Bots: []combat.FightBot{tb(861, "Fizz", "warrior", "dps", 100, 0, 1, 0, 0)}}
	mem := &fakeMem{
		rows:   []combat.FightSummary{memRow(key, f, 1)},
		fights: map[string]combat.Fight{key: f},
		warns:  map[string][]combat.Warning{key: {{Rule: "note", Level: "yellow", Bot: "Fizz", Role: "dps", Text: "Fizz note"}}},
	}
	target := "/api/v1/combat/underperformers?commander=T&day=2026-11-01"

	none := Handler(Sources{Mem: mem})
	var raw struct {
		Bots []map[string]any `json:"bots"`
	}
	rec := get(t, none, target)
	if rec.Code != http.StatusOK {
		t.Fatalf("status %d", rec.Code)
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &raw); err != nil {
		t.Fatalf("decode: %v", err)
	}
	if _, has := raw.Bots[0]["spec"]; has {
		t.Errorf("bots[0] has a spec key with no Specs source: %v", raw.Bots[0])
	}

	calls := 0
	specs := func(guids []uint32) map[uint32]string {
		calls++
		return map[uint32]string{861: "Fury"}
	}
	h := Handler(Sources{Mem: mem, Specs: specs})
	r := decodeAnswer(t, get(t, h, target))
	if len(r.Bots) != 1 || r.Bots[0].Spec != "Fury" {
		t.Fatalf("bots = %+v, want Fizz with spec Fury", r.Bots)
	}
	decodeAnswer(t, get(t, h, target))
	if calls != 1 {
		t.Errorf("Specs called %d times, want once (the second request is cached)", calls)
	}
}

func TestRunLast(t *testing.T) {
	base := dayOf(2026, 11, 1)
	oldKey, newKey := combat.Key("T", 1, "old"), combat.Key("T", 1, "new")
	oldF := combat.Fight{ID: "old", Start: base + 3600, DurMS: 30000, Bots: []combat.FightBot{tb(1, "Old", "rogue", "dps", 3000, 0, 10, 0, 0)}}
	newF := combat.Fight{ID: "new", Start: base + 7200, DurMS: 30000, Bots: []combat.FightBot{tb(2, "New", "rogue", "dps", 3000, 0, 10, 0, 0)}}
	mem := &fakeMem{
		rows:   []combat.FightSummary{memRow(newKey, newF, 1), memRow(oldKey, oldF, 1)},
		fights: map[string]combat.Fight{oldKey: oldF, newKey: newF},
		warns: map[string][]combat.Warning{
			oldKey: {{Rule: "note", Level: "yellow", Bot: "Old", Role: "dps", Text: "Old note"}},
			newKey: {{Rule: "note", Level: "yellow", Bot: "New", Role: "dps", Text: "New note"}},
		},
	}
	r := decodeAnswer(t, get(t, Handler(Sources{Mem: mem}), "/api/v1/combat/underperformers?commander=T&day=2026-11-01&run=last"))
	if r.Run != "last" || r.Fights != 1 {
		t.Fatalf("run = %q fights = %d, want last and 1", r.Run, r.Fights)
	}
	if len(r.Runs) != 2 {
		t.Fatalf("runs = %+v, want both runs of the window", r.Runs)
	}
	if len(r.Bots) != 1 || r.Bots[0].Name != "New" {
		t.Fatalf("bots = %+v, want only the newer run's New", r.Bots)
	}
	if r.Since != newF.Start {
		t.Errorf("since = %d, want the run's start %d", r.Since, newF.Start)
	}
}
