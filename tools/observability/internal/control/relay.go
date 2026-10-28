package control

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"strings"
	"sync"
	"time"
)

const (
	maxRequestBytes = 512
	maxAnswerBytes  = 4000
	maxAuditBytes   = 200
)

// Relay is an HTTP handler forwarding allow-listed GM lines to the game
// server's SOAP endpoint. One call runs at a time; the password never reaches
// the log, the error texts or the answer.
type Relay struct {
	Client    *SOAPClient
	MinGM     uint32
	AuditPath string
	Session   func(*http.Request) (user string, rank uint32, err error)
	Timeout   time.Duration

	mu   sync.Mutex
	busy bool
}

func (r *Relay) timeout() time.Duration {
	if r.Timeout > 0 {
		return r.Timeout
	}
	if r.Client != nil && r.Client.Timeout > 0 {
		return r.Client.Timeout
	}
	return 10 * time.Second
}

func (r *Relay) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	if req.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if !sameOrigin(req) {
		http.Error(w, "forbidden: origin", http.StatusForbidden)
		return
	}
	if req.Header.Get("X-TBM-Control") != "1" {
		http.Error(w, "forbidden: header", http.StatusForbidden)
		return
	}
	user, rank, err := r.Session(req)
	if err != nil {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}
	if rank < r.MinGM {
		r.audit(user, "", false, "rank")
		http.Error(w, "forbidden: rank", http.StatusForbidden)
		return
	}
	raw, err := io.ReadAll(io.LimitReader(req.Body, maxRequestBytes+1))
	if err != nil {
		http.Error(w, "bad request", http.StatusBadRequest)
		return
	}
	if len(raw) > maxRequestBytes {
		r.audit(user, "", false, "too large")
		http.Error(w, "bad request", http.StatusBadRequest)
		return
	}
	var in struct {
		Cmd string `json:"cmd"`
	}
	if err := json.Unmarshal(raw, &in); err != nil || strings.TrimSpace(in.Cmd) == "" {
		r.audit(user, "", false, "bad request")
		http.Error(w, "bad request", http.StatusBadRequest)
		return
	}
	line, ok := Allowed(in.Cmd)
	if !ok {
		r.audit(user, clip(in.Cmd, maxAuditBytes), false, "not allowed")
		writeErr(w, "not allowed")
		return
	}
	if !r.take() {
		r.audit(user, line, false, "busy")
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusTooManyRequests)
		_ = json.NewEncoder(w).Encode(map[string]any{"ok": false, "error": "busy"})
		return
	}
	defer r.release()
	ctx := req.Context()
	client := *r.Client
	client.Timeout = r.timeout()
	answer, err := client.Run(ctx, line)
	if err != nil {
		msg := "soap error"
		if isTimeout(err) {
			w.WriteHeader(http.StatusGatewayTimeout)
			msg = "timeout"
		} else {
			w.WriteHeader(http.StatusBadGateway)
		}
		r.audit(user, line, false, msg)
		writeErrRaw(w, msg)
		return
	}
	r.audit(user, line, true, clip(answer, maxAuditBytes))
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "answer": clip(answer, maxAnswerBytes)})
}

func (r *Relay) take() bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.busy {
		return false
	}
	r.busy = true
	return true
}

func (r *Relay) release() {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.busy = false
}

func sameOrigin(req *http.Request) bool {
	origin := req.Header.Get("Origin")
	if origin == "" {
		return false
	}
	u, err := url.Parse(origin)
	if err != nil {
		return false
	}
	if !strings.EqualFold(u.Host, req.Host) {
		return false
	}
	return u.Scheme == "http" || u.Scheme == "https"
}

func isTimeout(err error) bool {
	if err == nil {
		return false
	}
	if os.IsTimeout(err) {
		return true
	}
	msg := strings.ToLower(err.Error())
	return strings.Contains(msg, "deadline exceeded") || strings.Contains(msg, "timeout")
}

func writeErr(w http.ResponseWriter, msg string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusBadRequest)
	_ = json.NewEncoder(w).Encode(map[string]any{"ok": false, "error": msg})
}

func writeErrRaw(w http.ResponseWriter, msg string) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"ok": false, "error": msg})
}

func clip(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n]
}

func (r *Relay) audit(user, cmd string, ok bool, answer string) {
	if r.AuditPath == "" {
		return
	}
	line, err := json.Marshal(map[string]any{
		"t":      time.Now().Unix(),
		"user":   user,
		"cmd":    cmd,
		"ok":     ok,
		"answer": answer,
	})
	if err != nil {
		return
	}
	f, err := os.OpenFile(r.AuditPath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o600)
	if err != nil {
		return
	}
	defer f.Close()
	fmt.Fprintln(f, string(line))
}
