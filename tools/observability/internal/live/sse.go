package live

import (
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"sort"
	"time"

	"tortoise-observability/internal/ws"
)

// Options tunes one stream. The zero value is not usable: Handler's caller
// passes the three durations (15 s heartbeat, 10 s write deadline, 3 s retry).
type Options struct {
	Heartbeat    time.Duration
	WriteTimeout time.Duration
	Retry        time.Duration
}

var errBadCommanders = errors.New("bad commander list")

// ParseCommanders reads the repeated ?commander= parameter: 1 to
// MaxCommandersPerClient names, each 1 to MaxCommanderNameBytes printable
// bytes, deduplicated and sorted.
func ParseCommanders(q url.Values) ([]string, error) {
	raw, ok := q["commander"]
	if !ok || len(raw) == 0 || len(raw) > MaxCommandersPerClient {
		return nil, errBadCommanders
	}
	seen := make(map[string]bool, len(raw))
	out := make([]string, 0, len(raw))
	for _, name := range raw {
		if len(name) == 0 || len(name) > MaxCommanderNameBytes {
			return nil, errBadCommanders
		}
		for i := range len(name) {
			if name[i] < 0x20 {
				return nil, errBadCommanders
			}
		}
		if seen[name] {
			continue
		}
		seen[name] = true
		out = append(out, name)
	}
	sort.Strings(out)
	return out, nil
}

// Handler serves one Server-Sent Events stream. initial builds the frames
// written before the first live frame (the snapshot), from the commanders the
// caller asked for.
func Handler(h *Hub, opt Options, initial func(commanders []string) []Frame) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !ws.SameOrigin(r) {
			http.Error(w, "forbidden", http.StatusForbidden)
			return
		}
		commanders, err := ParseCommanders(r.URL.Query())
		if err != nil {
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusBadRequest)
			_, _ = io.WriteString(w, `{"error":"bad commander list"}`)
			return
		}
		flusher, ok := w.(http.Flusher)
		if !ok {
			http.Error(w, "streaming unsupported", http.StatusInternalServerError)
			return
		}
		c, err := h.Subscribe(commanders)
		if err != nil {
			w.Header().Set("Retry-After", "10")
			http.Error(w, "too many live clients", http.StatusServiceUnavailable)
			return
		}
		defer h.Unsubscribe(c)

		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		w.Header().Set("X-Accel-Buffering", "no")
		// The server's WriteTimeout would cut a long-lived stream; every write
		// moves its own deadline instead.
		rc := http.NewResponseController(w)
		write := func(s string) bool {
			_ = rc.SetWriteDeadline(time.Now().Add(opt.WriteTimeout))
			if _, err := io.WriteString(w, s); err != nil {
				return false
			}
			flusher.Flush()
			return true
		}
		writeFrame := func(f Frame) bool {
			return write("event: " + f.Event + "\ndata: " + string(f.Data) + "\n\n")
		}

		if opt.Retry > 0 {
			if !write(fmt.Sprintf("retry: %d\n\n", opt.Retry.Milliseconds())) {
				return
			}
		}
		if initial != nil {
			for _, f := range initial(commanders) {
				if !writeFrame(f) {
					return
				}
			}
		}

		heartbeat := time.NewTicker(opt.Heartbeat)
		defer heartbeat.Stop()
		for {
			select {
			case <-r.Context().Done():
				return
			case <-heartbeat.C:
				if !write(": ping\n\n") {
					return
				}
			case f := <-c.Frames():
				if n := c.TakeDropped(); n > 0 {
					if !write(fmt.Sprintf("event: resync\ndata: {\"dropped\":%d}\n\n", n)) {
						return
					}
				}
				if !writeFrame(f) {
					return
				}
			}
		}
	}
}
