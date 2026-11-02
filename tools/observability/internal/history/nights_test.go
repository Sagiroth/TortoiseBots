package history

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

// A fight at 02:17 local on 2 Nov belongs to the night of 1 Nov: with tz set
// the days route groups by local night instead of UTC day.
func TestNightsGroupByLocalNight(t *testing.T) {
	clock := &fakeClock{t: t0}
	s := open(t, Config{Dir: t.TempDir(), Now: clock.Now})
	start := time.Date(2026, 10, 31, 22, 30, 0, 0, time.UTC).Unix() // 23:30 Oslo 31 Oct
	deliver(t, s, clock, Item{Session: 1, Fight: mkFight("n1", "Glacienne", start), At: clock.Now()})
	oslo, err := time.LoadLocation("Europe/Oslo")
	if err != nil {
		t.Fatal(err)
	}

	nights := s.Nights("Glacienne", oslo, 6)
	if len(nights) != 1 || nights[0].Night != "2026-10-31" || nights[0].Fights != 1 {
		t.Fatalf("Nights = %+v, want one night 2026-10-31 with one fight", nights)
	}
	if nights[0].First != start || nights[0].Last != start {
		t.Errorf("first/last = %d/%d, want %d", nights[0].First, nights[0].Last, start)
	}

	do := func(target string) (int, string) {
		rec := httptest.NewRecorder()
		DaysHandler(s)(rec, httptest.NewRequest(http.MethodGet, target, nil))
		return rec.Code, rec.Body.String()
	}
	var body struct {
		Nights []NightCount `json:"nights"`
	}
	rec := httptest.NewRecorder()
	DaysHandler(s)(rec, httptest.NewRequest(http.MethodGet, "/api/v1/combat/history/days?commander=Glacienne&tz=Europe%2FOslo", nil))
	if rec.Code != 200 {
		t.Fatalf("days with tz: %d %s", rec.Code, rec.Body.String())
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil || len(body.Nights) != 1 || body.Nights[0].Night != "2026-10-31" || body.Nights[0].Fights != 1 {
		t.Errorf("days with tz = %s, err %v", rec.Body.String(), err)
	}
	if code, b := do("/api/v1/combat/history/days?commander=Glacienne"); code != 200 || !strings.Contains(b, `{"days":[{"day":"2026-10-31","fights":1}]}`) {
		t.Errorf("days without tz: %d %s, want the old UTC rows", code, b)
	}
	if code, b := do("/api/v1/combat/history/days?commander=Glacienne&tz=Nope%2FZone"); code != 400 || !strings.Contains(b, "bad tz") {
		t.Errorf("days with bad tz: %d %s, want 400", code, b)
	}
	if code, b := do("/api/v1/combat/history/days?commander=Glacienne&tz=Europe%2FOslo&rollover=13"); code != 400 || !strings.Contains(b, "bad rollover") {
		t.Errorf("days with bad rollover: %d %s, want 400", code, b)
	}
}

// A night spans two UTC files: the night list returns fights from both, and
// the neighbour night's early fight stays out.
func TestListNightSpansTwoUTCDays(t *testing.T) {
	clock := &fakeClock{t: t0}
	s := open(t, Config{Dir: t.TempDir(), Now: clock.Now})
	a := time.Date(2026, 11, 1, 22, 0, 0, 0, time.UTC).Unix() // 23:00 Oslo 1 Nov, the night's own
	b := time.Date(2026, 11, 2, 3, 0, 0, 0, time.UTC).Unix()  // 04:00 Oslo 2 Nov, still the night
	c := time.Date(2026, 11, 1, 4, 0, 0, 0, time.UTC).Unix()  // 05:00 Oslo 1 Nov, the night before
	deliver(t, s, clock, Item{Session: 1, Fight: mkFight("a", "Glacienne", a), At: clock.Now()})
	deliver(t, s, clock, Item{Session: 1, Fight: mkFight("b", "Glacienne", b), At: clock.Now()})
	deliver(t, s, clock, Item{Session: 1, Fight: mkFight("c", "Glacienne", c), At: clock.Now()})
	oslo, err := time.LoadLocation("Europe/Oslo")
	if err != nil {
		t.Fatal(err)
	}

	rows := s.ListNight("Glacienne", "2026-11-01", oslo, 6)
	if len(rows) != 2 || rows[0].ID != "b" || rows[1].ID != "a" {
		t.Fatalf("ListNight = %+v, want b then a", rows)
	}

	do := func(target string) (int, string) {
		rec := httptest.NewRecorder()
		ListHandler(s)(rec, httptest.NewRequest(http.MethodGet, target, nil))
		return rec.Code, rec.Body.String()
	}
	if code, b := do("/api/v1/combat/history?commander=Glacienne&tz=Europe%2FOslo&rollover=6&night=2026-11-01"); code != 200 ||
		!strings.Contains(b, `"id":"b"`) || !strings.Contains(b, `"id":"a"`) || strings.Contains(b, `"id":"c"`) {
		t.Errorf("night list: %d %s, want b and a without c", code, b)
	}
	if code, b := do("/api/v1/combat/history?commander=Glacienne&night=2026-11-01"); code != 400 {
		t.Errorf("night without tz: %d %s, want 400", code, b)
	}
	if code, b := do("/api/v1/combat/history?commander=Glacienne&tz=Europe%2FOslo&night=2026-11-01"); code != 200 ||
		!strings.Contains(b, `"id":"b"`) || !strings.Contains(b, `"id":"a"`) {
		t.Errorf("night with default rollover: %d %s, want b and a", code, b)
	}
}
