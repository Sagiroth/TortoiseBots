package underperf

import (
	"context"
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

// A fight the memory store lists but no longer holds when the detail is read
// (evicted between List and GetKey) must still reach the report from history.
func TestEvictedBetweenListAndGetKeyStaysInReport(t *testing.T) {
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
		fights: map[string]combat.Fight{}, // GetKey fails for the listed key
	}
	hist := &fakeHist{
		rows:   []history.Row{{Key: key, Commander: "T", Session: 7, Start: start}},
		fights: map[string]combat.Fight{key: f},
	}
	rules := func() []combat.Rule { return rulesOf(t) }
	h := Handler(Sources{Mem: mem, Hist: hist, Rules: rules})
	r := decodeAnswer(t, get(t, h, "/api/v1/combat/underperformers?commander=T&day=2026-11-01"))
	if r.Fights != 1 {
		t.Fatalf("fights = %d, want 1 (evicted fight must come from history)", r.Fights)
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
		"/api/v1/combat/underperformers?commander=T&night=2026-11-01",
		"/api/v1/combat/underperformers?commander=T&night=2026-11-1&tz=Europe%2FOslo",
		"/api/v1/combat/underperformers?commander=T&night=2026-11-01&tz=Europe%2FOslo&rollover=13",
		"/api/v1/combat/underperformers?commander=T&night=2026-11-01&tz=Europe%2FOslo&day=2026-11-01",
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

// nightTestNow keeps the store's retention and write delay off the wall clock: the fights below sit on fixed DST dates.
var nightTestNow = time.Date(2026, 11, 2, 12, 0, 0, 0, time.UTC)

// openHistory is a real store, so the night route is checked against the same index the history list reads.
func openHistory(t *testing.T) *history.Store {
	t.Helper()
	s, err := history.Open(history.Config{Dir: t.TempDir(), Now: func() time.Time { return nightTestNow }},
		func(context.Context, string, combat.Fight) int { return 0 },
		func(string, combat.Fight) (int64, int64, bool) { return 0, 0, false })
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { s.Close(context.Background()) })
	return s
}

// A night is the history list's night: fights across UTC midnight and the
// 25-hour fall-back night are in it exactly while nightOf puts them there.
func TestNightWindowMatchesHistoryList(t *testing.T) {
	oslo, err := time.LoadLocation("Europe/Oslo")
	if err != nil {
		t.Fatal(err)
	}
	st := openHistory(t)
	fights := []struct {
		id    string
		start time.Time
	}{
		{"early", time.Date(2026, 10, 24, 3, 59, 0, 0, time.UTC)}, // 05:59 CEST, night of 23 Oct
		{"mid", time.Date(2026, 10, 24, 4, 0, 0, 0, time.UTC)},    // 06:00 CEST, night of 24 Oct
		{"late", time.Date(2026, 10, 25, 0, 30, 0, 0, time.UTC)},  // 02:30 CEST, 24 Oct though the UTC date moved on
		{"edge", time.Date(2026, 10, 25, 4, 59, 0, 0, time.UTC)},  // 05:59 CET after the fall-back, still 24 Oct
		{"next", time.Date(2026, 10, 25, 5, 0, 0, 0, time.UTC)},   // 06:00 CET, night of 25 Oct
	}
	for _, in := range fights {
		f := combat.Fight{ID: in.id, Commander: "T", Start: in.start.Unix(), DurMS: 60000,
			Bots: []combat.FightBot{tb(1, "A", "rogue", "dps", 3000, 0, 10, 0, 0)}}
		if !st.Enqueue(history.Item{Session: 1, Fight: f, At: nightTestNow.Add(-time.Minute)}) {
			t.Fatal("enqueue refused")
		}
	}
	st.Flush()

	h := Handler(Sources{Hist: st})
	want := map[string]int{"2026-10-23": 1, "2026-10-24": 3, "2026-10-25": 1}
	for night, n := range want {
		target := "/api/v1/combat/underperformers?commander=T&night=" + night + "&tz=Europe%2FOslo&rollover=6"
		r := decodeAnswer(t, get(t, h, target))
		if r.Fights != n {
			t.Errorf("night %s: fights = %d, want %d", night, r.Fights, n)
		}
		if got := len(st.ListNight("T", night, oslo, 6)); r.Fights != got {
			t.Errorf("night %s: fights = %d, the history night list has %d", night, r.Fights, got)
		}
	}

	// An unknown zone falls back to that UTC day, as the history list does.
	rec := get(t, h, "/api/v1/combat/underperformers?commander=T&night=2026-10-25&tz=Nope%2FZone&rollover=6")
	if rec.Header().Get("X-TZ-Fallback") != "utc" {
		t.Errorf("unknown zone: no X-TZ-Fallback: utc header")
	}
	r := decodeAnswer(t, rec)
	if got := len(st.List("T", "2026-10-25")); r.Fights != got || r.Fights != 3 {
		t.Errorf("unknown zone: fights = %d, want the UTC day's %d", r.Fights, got)
	}
	if r.Since != dayOf(2026, 10, 25) || r.Until != dayOf(2026, 10, 26) {
		t.Errorf("unknown zone: window = [%d, %d), want the UTC day", r.Since, r.Until)
	}
}

// A run is one session's fights: an overlapping fight of another session
// inside the run's time bounds is not part of it.
func TestRunFilterKeepsOneSession(t *testing.T) {
	base := dayOf(2026, 11, 1)
	mk := func(id string, guid uint32, name string, start int64, dur uint32) (string, combat.Fight) {
		key := combat.Key("T", 1, id)
		return key, combat.Fight{ID: id, Start: start, DurMS: dur,
			Bots: []combat.FightBot{tb(guid, name, "rogue", "dps", 3000, 0, 10, 0, 0)}}
	}
	a1Key, a1 := mk("a1", 1, "A", base+3600, 60000)
	b1Key, b1 := mk("b1", 2, "B", base+3630, 30000)
	a2Key, a2 := mk("a2", 3, "C", base+3900, 30000)
	mem := &fakeMem{
		rows: []combat.FightSummary{
			memRow(a1Key, a1, 1), memRow(b1Key, b1, 2), memRow(a2Key, a2, 1),
		},
		fights: map[string]combat.Fight{a1Key: a1, b1Key: b1, a2Key: a2},
		warns:  map[string][]combat.Warning{a1Key: nil, b1Key: nil, a2Key: nil},
	}
	h := Handler(Sources{Mem: mem})

	r := decodeAnswer(t, get(t, h, "/api/v1/combat/underperformers?commander=T&day=2026-11-01&run=0"))
	if r.Fights != 1 || !onlyNames(r, "A") {
		t.Errorf("run 0 = %d fights, bots %v + ok %v; want only session 1's A", r.Fights, namesOf(r.Bots), namesOfOK(r.OK))
	}

	r = decodeAnswer(t, get(t, h, "/api/v1/combat/underperformers?commander=T&day=2026-11-01&run=1"))
	if r.Fights != 1 || !onlyNames(r, "B") {
		t.Errorf("run 1 = %d fights, bots %v + ok %v; want only session 2's B", r.Fights, namesOf(r.Bots), namesOfOK(r.OK))
	}
}

func onlyNames(r Report, want string) bool {
	got := append(namesOf(r.Bots), namesOfOK(r.OK)...)
	return len(got) == 1 && got[0] == want
}

func namesOf(bots []BotRow) []string {
	out := make([]string, len(bots))
	for i := range bots {
		out[i] = bots[i].Name
	}
	return out
}

func namesOfOK(rows []OKRow) []string {
	out := make([]string, len(rows))
	for i := range rows {
		out[i] = rows[i].Name
	}
	return out
}

func TestTruncatedRunKeepsFlag(t *testing.T) {
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
	r := decodeAnswer(t, get(t, Handler(Sources{Mem: mem}), "/api/v1/combat/underperformers?commander=T&day=2026-11-02&run=last"))
	if r.Run != "last" {
		t.Fatalf("run = %q, want last", r.Run)
	}
	if r.Fights != 500 {
		t.Fatalf("fights = %d, want 500", r.Fights)
	}
	if !r.Truncated {
		t.Errorf("truncated = false on a run cut by the 500-fight cap, want true")
	}
}
