package history

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

// TestCommandersCarriesUnixLast pins the Saved fights list's clock: the row's
// last is the newest fight's unix start, so the page formats it in the
// viewer's own zone. A preformatted string would freeze the daemon's zone.
func TestCommandersCarriesUnixLast(t *testing.T) {
	clock := &fakeClock{t: t0}
	s := open(t, Config{Dir: t.TempDir(), Now: clock.Now})
	start := t0.Unix() - 60
	deliver(t, s, clock, Item{Session: 1, Fight: mkFight("f1", "Glacienne", start), At: clock.Now()})

	got := s.Commanders()
	if len(got) != 1 || got[0].Last != start {
		t.Fatalf("commanders = %+v, want last %d", got, start)
	}

	rec := httptest.NewRecorder()
	CommandersHandler(s)(rec, httptest.NewRequest(http.MethodGet, "/api/v1/combat/history/commanders", nil))
	var body struct {
		Commanders []map[string]any `json:"commanders"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil || len(body.Commanders) != 1 {
		t.Fatalf("commanders handler = %d %s, err %v", rec.Code, rec.Body.String(), err)
	}
	if body.Commanders[0]["last"] != float64(start) {
		t.Errorf("handler last = %v, want %d", body.Commanders[0]["last"], start)
	}
	if _, ok := body.Commanders[0]["last_str"]; ok {
		t.Errorf("handler carries a preformatted string: %s", rec.Body.String())
	}
	if !strings.Contains(rec.Body.String(), `"last":`) {
		t.Errorf("handler drops the unix field: %s", rec.Body.String())
	}
}
