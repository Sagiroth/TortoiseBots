// Package live fans the realtime datagrams (positions, fight events) out to
// Server-Sent Events clients, one commander ask per client.
//
// Unlike the WebSocket hub, a client here asks for specific commanders and
// every publish is filtered by name, so the page only receives what it draws.
// A client's queue drops the OLDEST frame when it is full: these frames carry
// positions and events, where the newest is the one that matters, and the
// client is told through the resync frame. Publish is called from the UDP
// read goroutine only; the single producer lets the queue stay lock-free
// apart from the client map.
package live

import (
	"encoding/json"
	"errors"
	"sync"
	"sync/atomic"
)

const (
	// MaxClients bounds the open SSE streams; the page keeps one.
	MaxClients = 32
	// ClientQueue is the frames held per client; past it the oldest frame
	// goes and the loss is counted for the resync frame.
	ClientQueue = 64
	// MaxCommandersPerClient is how many commanders one stream may ask for.
	MaxCommandersPerClient = 20
	// MaxCommanderNameBytes bounds one commander name in the query.
	MaxCommanderNameBytes = 64
)

// ErrTooManyClients is returned when MaxClients streams are already open.
var ErrTooManyClients = errors.New("too many live clients")

// Frame is one SSE event, marshalled once per Publish.
type Frame struct {
	Event string
	Data  []byte
}

// Client is one open stream: the commanders it asked for and its queue.
type Client struct {
	want    map[string]bool
	ch      chan Frame
	dropped atomic.Uint64
}

// Frames is the client's queue; the reader returns on its own context.
func (c *Client) Frames() <-chan Frame { return c.ch }

// TakeDropped reports how many frames this client lost and resets the count.
func (c *Client) TakeDropped() uint64 { return c.dropped.Swap(0) }

func (c *Client) push(f Frame, hubDropped *atomic.Uint64) {
	select {
	case c.ch <- f:
		return
	default:
	}
	select {
	case <-c.ch:
		c.dropped.Add(1)
		hubDropped.Add(1)
	default:
	}
	select {
	case c.ch <- f:
	default:
		c.dropped.Add(1)
	}
}

// Hub is the set of open streams and the publish side of the live feed.
type Hub struct {
	mu      sync.Mutex
	clients map[*Client]struct{}
	dropped atomic.Uint64
}

// NewHub creates an empty hub.
func NewHub() *Hub {
	return &Hub{clients: make(map[*Client]struct{})}
}

// Subscribe registers one stream for the given commanders. It never blocks
// and returns ErrTooManyClients past MaxClients.
func (h *Hub) Subscribe(commanders []string) (*Client, error) {
	c := &Client{want: make(map[string]bool, len(commanders)), ch: make(chan Frame, ClientQueue)}
	for _, n := range commanders {
		c.want[n] = true
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	if len(h.clients) >= MaxClients {
		return nil, ErrTooManyClients
	}
	h.clients[c] = struct{}{}
	return c, nil
}

// Unsubscribe removes a stream. It is idempotent and never closes the
// client's channel: the reader goroutine returns on its request context.
func (h *Hub) Unsubscribe(c *Client) {
	h.mu.Lock()
	defer h.mu.Unlock()
	delete(h.clients, c)
}

// Publish marshals one payload and offers it to every client that asked for
// the commander. It holds the lock only to copy the matching clients, so a
// slow reader never delays the UDP read loop.
func (h *Hub) Publish(commander, event string, payload interface{}) {
	data, err := json.Marshal(payload)
	if err != nil {
		return
	}
	f := Frame{Event: event, Data: data}
	h.mu.Lock()
	targets := make([]*Client, 0, len(h.clients))
	for c := range h.clients {
		if c.want[commander] {
			targets = append(targets, c)
		}
	}
	h.mu.Unlock()
	for _, c := range targets {
		c.push(f, &h.dropped)
	}
}

// Clients is the number of open streams.
func (h *Hub) Clients() int {
	h.mu.Lock()
	defer h.mu.Unlock()
	return len(h.clients)
}

// Dropped is the number of frames lost to full queues since start.
func (h *Hub) Dropped() uint64 { return h.dropped.Load() }
