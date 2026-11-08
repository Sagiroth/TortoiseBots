package live

import (
	"errors"
	"testing"
)

// want reads one frame from a client without blocking a passing test.
func wantFrame(t *testing.T, c *Client) Frame {
	t.Helper()
	select {
	case f := <-c.Frames():
		return f
	default:
		t.Fatal("no frame delivered")
		return Frame{}
	}
}

func TestPublishFiltersByCommander(t *testing.T) {
	h := NewHub()
	alice, err := h.Subscribe([]string{"Alice"})
	if err != nil {
		t.Fatal(err)
	}
	bob, err := h.Subscribe([]string{"Bob"})
	if err != nil {
		t.Fatal(err)
	}
	h.Publish("Alice", "pos", map[string]int{"x": 1})

	f := wantFrame(t, alice)
	if f.Event != "pos" || string(f.Data) != `{"x":1}` {
		t.Fatalf("alice got %q %s", f.Event, f.Data)
	}
	select {
	case f := <-bob.Frames():
		t.Fatalf("bob asked for Bob but got %q", f.Event)
	default:
	}
	if h.Dropped() != 0 {
		t.Fatalf("dropped: %d", h.Dropped())
	}
}

// TestDropOldest: an unread queue of 64 keeps the newest frames; the oldest
// six are dropped and counted on the client, not in the hub.
func TestDropOldest(t *testing.T) {
	h := NewHub()
	c, err := h.Subscribe([]string{"Alice"})
	if err != nil {
		t.Fatal(err)
	}
	for i := 1; i <= 70; i++ {
		h.Publish("Alice", "pos", i)
	}
	if n := c.TakeDropped(); n != 6 {
		t.Fatalf("TakeDropped: %d, want 6", n)
	}
	if n := c.TakeDropped(); n != 0 {
		t.Fatalf("TakeDropped must swap to 0, got %d", n)
	}
	first := wantFrame(t, c)
	if string(first.Data) != "7" {
		t.Fatalf("first kept frame %s, want 7", first.Data)
	}
	n := 1
	for {
		select {
		case <-c.Frames():
			n++
			continue
		default:
		}
		break
	}
	if n != ClientQueue {
		t.Fatalf("kept %d frames, want %d", n, ClientQueue)
	}
}

func TestMaxClients(t *testing.T) {
	h := NewHub()
	for i := range MaxClients {
		if _, err := h.Subscribe([]string{"Alice"}); err != nil {
			t.Fatalf("client %d: %v", i, err)
		}
	}
	if _, err := h.Subscribe([]string{"Alice"}); !errors.Is(err, ErrTooManyClients) {
		t.Fatalf("33rd client: %v, want ErrTooManyClients", err)
	}
	if h.Clients() != MaxClients {
		t.Fatalf("clients: %d", h.Clients())
	}
	// Freeing one makes room again.
	var last *Client
	for c := range h.clients {
		last = c
		break
	}
	h.Unsubscribe(last)
	if h.Clients() != MaxClients-1 {
		t.Fatalf("after unsubscribe: %d", h.Clients())
	}
	if _, err := h.Subscribe([]string{"Alice"}); err != nil {
		t.Fatalf("after freeing one: %v", err)
	}
}

func TestUnsubscribeIdempotent(t *testing.T) {
	h := NewHub()
	c, err := h.Subscribe([]string{"Alice"})
	if err != nil {
		t.Fatal(err)
	}
	h.Unsubscribe(c)
	h.Unsubscribe(c)
	if h.Clients() != 0 {
		t.Fatalf("clients: %d", h.Clients())
	}
	// The channel is never closed: a publish after the unsubscribe reaches
	// nobody and cannot panic on a closed channel.
	h.Publish("Alice", "pos", 1)
	select {
	case f := <-c.Frames():
		t.Fatalf("an unsubscribed client got %q", f.Event)
	default:
	}
}

// countingPayload counts how many times it is marshalled.
type countingPayload struct{ n *int }

func (p countingPayload) MarshalJSON() ([]byte, error) {
	*p.n++
	return []byte(`{"ok":1}`), nil
}

// TestPublishSkipsMarshalWithoutClients: with nobody watching the commander
// the payload is never marshalled; with watchers it is marshalled exactly once
// and shared.
func TestPublishSkipsMarshalWithoutClients(t *testing.T) {
	h := NewHub()
	calls := 0
	h.Publish("Alice", "pos", countingPayload{&calls})
	if calls != 0 {
		t.Fatalf("no clients: %d marshal calls, want 0", calls)
	}
	other, err := h.Subscribe([]string{"Bob"})
	if err != nil {
		t.Fatal(err)
	}
	h.Publish("Alice", "pos", countingPayload{&calls})
	if calls != 0 {
		t.Fatalf("client of another commander: %d marshal calls, want 0", calls)
	}
	if h.Wants("Alice") || !h.Wants("Bob") {
		t.Fatal("Wants must follow the subscriptions")
	}
	select {
	case <-other.Frames():
		t.Fatal("bob got alice's frame")
	default:
	}
	a1, _ := h.Subscribe([]string{"Alice"})
	a2, _ := h.Subscribe([]string{"Alice", "Bob"})
	h.Publish("Alice", "pos", countingPayload{&calls})
	if calls != 1 {
		t.Fatalf("two clients: %d marshal calls, want 1", calls)
	}
	for _, c := range []*Client{a1, a2} {
		if f := wantFrame(t, c); string(f.Data) != `{"ok":1}` {
			t.Fatalf("frame %s", f.Data)
		}
	}
}
