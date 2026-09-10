// Package ws provides a fan-out hub that decouples telemetry ingestion from
// client delivery.
//
// Every connected client owns a bounded send queue and a single writer
// goroutine. Broadcast never blocks on a socket: if a client cannot keep up,
// its message is dropped (the next snapshot re-synchronizes it) and the
// ingestion loop keeps running. gorilla/websocket connections are only ever
// written from that client's writer goroutine.
package ws

import (
	"encoding/json"
	"log"
	"net/http"
	"sync"
	"sync/atomic"
	"time"

	"github.com/gorilla/websocket"
)

const (
	sendQueueDepth = 16
	writeTimeout   = 10 * time.Second
	// pongWait bounds how long a half-open connection can linger.
	pongWait = 60 * time.Second
	pingWait = 25 * time.Second
)

// Event is one message delivered to a newly connected client.
type Event struct {
	Name string
	Data interface{}
}

type client struct {
	conn *websocket.Conn
	send chan []byte
}

// Hub fans messages out to all connected dashboard clients.
type Hub struct {
	mu      sync.RWMutex
	clients map[*client]struct{}

	dropped uint64
}

// NewHub creates an empty hub.
func NewHub() *Hub {
	return &Hub{clients: make(map[*client]struct{})}
}

// Broadcast marshals one envelope and enqueues it for every client. Clients
// with a full queue are skipped; the daemon never blocks on a slow reader.
func (h *Hub) Broadcast(event string, payload interface{}) {
	envelope, err := json.Marshal(map[string]interface{}{
		"event": event,
		"data":  payload,
	})
	if err != nil {
		return
	}

	h.mu.RLock()
	defer h.mu.RUnlock()

	for c := range h.clients {
		select {
		case c.send <- envelope:
		default:
			// Client is slower than the telemetry stream. Dropping is safe:
			// snapshots are self-contained and the next one repairs state.
			atomic.AddUint64(&h.dropped, 1)
		}
	}
}

// Dropped reports how many messages were skipped for slow clients.
func (h *Hub) Dropped() uint64 {
	return atomic.LoadUint64(&h.dropped)
}

// Serve upgrades the request and serves the connection until it closes.
// initial events are enqueued before any broadcast can race with them.
func (h *Hub) Serve(w http.ResponseWriter, r *http.Request, upgrader websocket.Upgrader, initial ...Event) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("[WS] Upgrade error: %v", err)
		return
	}

	c := &client{conn: conn, send: make(chan []byte, sendQueueDepth)}

	// Enqueue the bootstrap before registering, so no live broadcast can
	// overtake the initial snapshot on the wire.
	for _, ev := range initial {
		envelope, err := json.Marshal(map[string]interface{}{"event": ev.Name, "data": ev.Data})
		if err != nil {
			continue
		}
		select {
		case c.send <- envelope:
		default:
		}
	}

	h.mu.Lock()
	h.clients[c] = struct{}{}
	h.mu.Unlock()

	go h.writeLoop(c)
	go h.readLoop(c)
}

// Remove unregisters a client and closes its connection. It is safe to call
// more than once; the send channel is only closed under the hub lock, and
// Broadcast holds the same lock, so sends can never race with the close.
func (h *Hub) Remove(c *client) {
	h.mu.Lock()
	if _, ok := h.clients[c]; !ok {
		h.mu.Unlock()
		return
	}
	delete(h.clients, c)
	close(c.send)
	h.mu.Unlock()

	_ = c.conn.Close()
}

// writeLoop is the only goroutine that writes to the connection: telemetry
// messages and keepalive pings are serialized here.
func (h *Hub) writeLoop(c *client) {
	ping := time.NewTicker(pingWait)
	defer ping.Stop()

	for {
		select {
		case msg, ok := <-c.send:
			if !ok {
				return
			}
			_ = c.conn.SetWriteDeadline(time.Now().Add(writeTimeout))
			if err := c.conn.WriteMessage(websocket.TextMessage, msg); err != nil {
				h.Remove(c)
				return
			}
		case <-ping.C:
			_ = c.conn.SetWriteDeadline(time.Now().Add(writeTimeout))
			if err := c.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				h.Remove(c)
				return
			}
		}
	}
}

func (h *Hub) readLoop(c *client) {
	_ = c.conn.SetReadDeadline(time.Now().Add(pongWait))
	c.conn.SetPongHandler(func(string) error {
		return c.conn.SetReadDeadline(time.Now().Add(pongWait))
	})

	for {
		if _, _, err := c.conn.ReadMessage(); err != nil {
			h.Remove(c)
			return
		}
	}
}
