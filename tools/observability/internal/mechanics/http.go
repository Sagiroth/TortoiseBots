package mechanics

import (
	"encoding/json"
	"net/http"
	"time"

	"tortoise-observability/internal/underperf"
)

// Window is the half-open [since, until) of fight start times (unix seconds) an answer covers.
type Window struct {
	Since int64 `json:"since"`
	Until int64 `json:"until"`
}

// Answer is the body of GET /api/v1/combat/mechanics.
type Answer struct {
	Rows   []Row  `json:"rows"`
	Fights int    `json:"fights"` // the fights the rows count (boss fights of at least MinFightMS)
	Window Window `json:"window"`
	// Truncated: the window held more fights than the daemon reads at once, so the oldest were left out.
	Truncated bool `json:"truncated,omitempty"`
}

// Handler serves GET /api/v1/combat/mechanics?commander=&night=|day=|since=: the scorecard of one commander's window. It reads
// the same sources and takes the same window parameters as the underperforming bots view (underperf.Window and
// underperf.Collect are shared; the auth wrapper is the caller's, as for that route).
func Handler(src underperf.Sources) http.HandlerFunc {
	nowFn := src.Now
	if nowFn == nil {
		nowFn = time.Now
	}
	return func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		commander := q.Get("commander")
		if commander == "" {
			writeErr(w, "commander is required")
			return
		}
		since, until, fallback, msg := underperf.Window(q, nowFn())
		if msg != "" {
			writeErr(w, msg)
			return
		}
		if fallback {
			w.Header().Set("X-TZ-Fallback", "utc")
		}
		// The warnings are not read here, so no rules are passed (a history fight is not re-judged for nothing).
		fights, truncated := underperf.Collect(src, commander, since, until, nil, underperf.DefaultLimits().MaxFights)
		in := make([]Input, len(fights))
		for i, f := range fights {
			in[i] = Input{Key: f.Key, Fight: f.Fight}
		}
		rows := Fold(in)
		n := 0
		for _, row := range rows {
			n += row.Fights
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(Answer{Rows: rows, Fights: n, Window: Window{since, until}, Truncated: truncated})
	}
}

func writeErr(w http.ResponseWriter, msg string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusBadRequest)
	_ = json.NewEncoder(w).Encode(struct {
		Error string `json:"error"`
	}{msg})
}
