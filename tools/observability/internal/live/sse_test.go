package live

import (
	"bufio"
	"bytes"
	"context"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"
)

func testOptions() Options {
	return Options{Heartbeat: time.Hour, WriteTimeout: 10 * time.Second, Retry: 3 * time.Second}
}

func snapshotFrames() func([]string) []Frame {
	return func(names []string) []Frame {
		return []Frame{{Event: "snapshot", Data: []byte(`{"commanders":[],"live":[],"updated_at":1,"asked":"` + strings.Join(names, ",") + `"}`)}}
	}
}

// readEvent reads one SSE block (the lines up to the blank line).
func readEvent(t *testing.T, r *bufio.Reader) string {
	t.Helper()
	var b strings.Builder
	for {
		line, err := r.ReadString('\n')
		b.WriteString(line)
		if err != nil {
			t.Fatalf("read: %v (got %q)", err, b.String())
		}
		if line == "\n" {
			return b.String()
		}
	}
}

func waitFor(t *testing.T, what string, cond func() bool) {
	t.Helper()
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		if cond() {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatalf("timed out waiting for %s", what)
}

func TestSSEHeadersAndSnapshotFirst(t *testing.T) {
	h := NewHub()
	srv := httptest.NewServer(Handler(h, testOptions(), snapshotFrames()))
	defer srv.Close()

	resp, err := http.Get(srv.URL + "/?commander=b&commander=a")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("status %d", resp.StatusCode)
	}
	if ct := resp.Header.Get("Content-Type"); !strings.HasPrefix(ct, "text/event-stream") {
		t.Fatalf("content-type %q", ct)
	}
	if resp.Header.Get("Cache-Control") != "no-cache" || resp.Header.Get("X-Accel-Buffering") != "no" {
		t.Fatalf("headers: %v", resp.Header)
	}
	r := bufio.NewReader(resp.Body)
	if got := readEvent(t, r); got != "retry: 3000\n\n" {
		t.Fatalf("first block %q", got)
	}
	first := readEvent(t, r)
	if !strings.HasPrefix(first, "event: snapshot\ndata: ") || !strings.Contains(first, `"asked":"a,b"`) {
		t.Fatalf("snapshot block %q", first)
	}
}

func TestSSEBadCommanders(t *testing.T) {
	hh := NewHub()
	handler := Handler(hh, testOptions(), nil)
	cases := map[string]string{
		"none":     "/",
		"21":       "/?" + strings.Repeat("commander=a&", 20) + "commander=t",
		"newline":  "/?commander=a%0Ab",
		"65 bytes": "/?commander=" + strings.Repeat("x", 65),
	}
	for name, target := range cases {
		w := httptest.NewRecorder()
		handler(w, httptest.NewRequest(http.MethodGet, target, nil))
		if w.Code != http.StatusBadRequest {
			t.Errorf("%s: status %d, want 400", name, w.Code)
		}
		if !strings.Contains(w.Body.String(), "bad commander list") {
			t.Errorf("%s: body %q", name, w.Body.String())
		}
	}
}

func TestSSECrossOrigin403(t *testing.T) {
	handler := Handler(NewHub(), testOptions(), nil)
	req := httptest.NewRequest(http.MethodGet, "/?commander=a", nil)
	req.Header.Set("Origin", "http://evil.example")
	w := httptest.NewRecorder()
	handler(w, req)
	if w.Code != http.StatusForbidden {
		t.Fatalf("status %d, want 403", w.Code)
	}
}

func TestSSEHeartbeat(t *testing.T) {
	h := NewHub()
	opt := testOptions()
	opt.Heartbeat = 50 * time.Millisecond
	srv := httptest.NewServer(Handler(h, opt, snapshotFrames()))
	defer srv.Close()

	resp, err := http.Get(srv.URL + "/?commander=a")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	r := bufio.NewReader(resp.Body)
	if got := readEvent(t, r); got != "retry: 3000\n\n" {
		t.Fatalf("first block %q", got)
	}
	if got := readEvent(t, r); !strings.HasPrefix(got, "event: snapshot") {
		t.Fatalf("snapshot block %q", got)
	}
	if got := readEvent(t, r); got != ": ping\n\n" {
		t.Fatalf("heartbeat block %q", got)
	}
}

func TestSSECleanupOnDisconnect(t *testing.T) {
	h := NewHub()
	srv := httptest.NewServer(Handler(h, testOptions(), snapshotFrames()))
	defer srv.Close()

	ctx, cancel := context.WithCancel(context.Background())
	req, _ := http.NewRequestWithContext(ctx, http.MethodGet, srv.URL+"/?commander=a", nil)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	r := bufio.NewReader(resp.Body)
	readEvent(t, r)
	readEvent(t, r)
	if h.Clients() != 1 {
		t.Fatalf("clients before the disconnect: %d", h.Clients())
	}
	cancel()
	resp.Body.Close()
	waitFor(t, "the client to be unsubscribed", func() bool { return h.Clients() == 0 })
}

// TestSSEOutlivesServerWriteTimeout: the server's WriteTimeout is 200 ms, so a
// long-lived stream only survives if every write moves its own deadline.
func TestSSEOutlivesServerWriteTimeout(t *testing.T) {
	h := NewHub()
	opt := testOptions()
	opt.Heartbeat = 50 * time.Millisecond
	srv := httptest.NewUnstartedServer(Handler(h, opt, snapshotFrames()))
	srv.Config.WriteTimeout = 200 * time.Millisecond
	srv.Start()
	defer srv.Close()

	resp, err := http.Get(srv.URL + "/?commander=a")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	r := bufio.NewReader(resp.Body)
	readEvent(t, r) // retry
	if got := readEvent(t, r); !strings.HasPrefix(got, "event: snapshot") {
		t.Fatalf("first block %q", got)
	}
	start := time.Now()
	pings := 0
	for time.Since(start) < 600*time.Millisecond {
		if readEvent(t, r) == ": ping\n\n" {
			pings++
		}
	}
	if pings == 0 {
		t.Fatal("no heartbeat after the server's 200 ms write timeout: every write must renew the deadline")
	}
}

// gateWriter freezes the handler inside a Write, so the test can fill the
// client's queue from the outside. It implements http.Flusher.
type gateWriter struct {
	mu      sync.Mutex
	header  http.Header
	buf     bytes.Buffer
	armed   bool
	gate    chan struct{}
	started chan struct{}
	once    *sync.Once
}

func newGateWriter() *gateWriter { return &gateWriter{header: http.Header{}} }

func (w *gateWriter) Header() http.Header { return w.header }
func (w *gateWriter) WriteHeader(int)     {}
func (w *gateWriter) Flush()              {}

func (w *gateWriter) Write(p []byte) (int, error) {
	w.mu.Lock()
	armed, gate, started, once := w.armed, w.gate, w.started, w.once
	w.mu.Unlock()
	if armed {
		if started != nil && once != nil {
			once.Do(func() { close(started) })
		}
		if gate != nil {
			<-gate
		}
	}
	w.mu.Lock()
	defer w.mu.Unlock()
	return w.buf.Write(p)
}

// arm makes the next writes block; it returns the gate to release them and a
// channel that is closed when the first blocked write starts.
func (w *gateWriter) arm() (chan struct{}, chan struct{}) {
	w.mu.Lock()
	defer w.mu.Unlock()
	w.armed = true
	w.gate = make(chan struct{})
	w.started = make(chan struct{})
	w.once = &sync.Once{}
	return w.gate, w.started
}

func (w *gateWriter) output() string {
	w.mu.Lock()
	defer w.mu.Unlock()
	return w.buf.String()
}

func TestSSEResyncAfterDrop(t *testing.T) {
	h := NewHub()
	w := newGateWriter()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	req := httptest.NewRequest(http.MethodGet, "/?commander=Alice", nil).WithContext(ctx)
	done := make(chan struct{})
	go func() {
		Handler(h, testOptions(), snapshotFrames())(w, req)
		close(done)
	}()
	waitFor(t, "the client to subscribe", func() bool { return h.Clients() == 1 })
	waitFor(t, "the snapshot", func() bool { return strings.Contains(w.output(), "event: snapshot") })

	gate, started := w.arm()
	h.Publish("Alice", "pos", "one")
	select {
	case <-started:
	case <-time.After(2 * time.Second):
		t.Fatal("the handler never started writing the first frame")
	}
	for i := 2; i <= 100; i++ {
		h.Publish("Alice", "pos", i)
	}
	close(gate)

	waitFor(t, "the resync frame", func() bool { return strings.Contains(w.output(), "event: resync") })
	out := w.output()
	if !strings.Contains(out, `event: resync`+"\ndata: "+`{"dropped":35}`) {
		t.Fatalf("resync frame missing the drop count:\n%s", out)
	}
	cancel()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("the handler did not return after the context ended")
	}
}

// The writer is also used directly: a handler that cannot flush must answer
// 500 rather than hang.
type noFlushWriter struct{ header http.Header }

func (w *noFlushWriter) Header() http.Header         { return w.header }
func (w *noFlushWriter) Write(p []byte) (int, error) { return len(p), nil }
func (w *noFlushWriter) WriteHeader(int)             {}

func TestSSENoFlusher500(t *testing.T) {
	w := &noFlushWriter{header: http.Header{}}
	Handler(NewHub(), testOptions(), nil)(w, httptest.NewRequest(http.MethodGet, "/?commander=a", nil))
	if w.header.Get("Content-Type") == "text/event-stream" {
		t.Fatal("a writer without Flush must not start a stream")
	}
}
