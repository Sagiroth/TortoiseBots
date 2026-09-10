// Package udp ingests telemetry datagrams from the game server and feeds the
// authoritative state store. Ingestion never blocks on WebSocket clients.
package udp

import (
	"encoding/json"
	"fmt"
	"log"
	"net"
	"time"

	zoneproject "tortoise-observability/internal/map"
	"tortoise-observability/internal/metrics"
	"tortoise-observability/internal/model"
	"tortoise-observability/internal/state"
)

// Hub is the broadcast surface the listener needs.
type Hub interface {
	Broadcast(event string, payload interface{})
}

// Listener owns the UDP socket and the janitor that evicts stale state.
type Listener struct {
	addr       string
	conn       *net.UDPConn
	store      *state.Store
	metricsReg *metrics.Registry
	zoneProj   *zoneproject.Engine
	hub        Hub
	stopChan   chan struct{}
}

func NewListener(host string, port int, store *state.Store, mr *metrics.Registry, zp *zoneproject.Engine, hub Hub) *Listener {
	return &Listener{
		addr:       fmt.Sprintf("%s:%d", host, port),
		store:      store,
		metricsReg: mr,
		zoneProj:   zp,
		hub:        hub,
		stopChan:   make(chan struct{}),
	}
}

func (l *Listener) Start() error {
	udpAddr, err := net.ResolveUDPAddr("udp", l.addr)
	if err != nil {
		return fmt.Errorf("failed to resolve UDP address %s: %w", l.addr, err)
	}

	conn, err := net.ListenUDP("udp", udpAddr)
	if err != nil {
		return fmt.Errorf("failed to listen on UDP %s: %w", l.addr, err)
	}
	l.conn = conn

	// Datagram bursts can carry a full roster of bots; a large receive buffer
	// keeps a busy world tick from overflowing the kernel queue.
	if err := l.conn.SetReadBuffer(1 << 20); err != nil {
		log.Printf("[UDP] Could not raise receive buffer: %v", err)
	}

	log.Printf("[UDP] Listening for TortoiseBots telemetry on %s", l.addr)

	go l.readLoop()
	go l.janitorLoop()
	return nil
}

func (l *Listener) Stop() {
	close(l.stopChan)
	if l.conn != nil {
		_ = l.conn.Close()
	}
}

func (l *Listener) readLoop() {
	buf := make([]byte, 65535)

	for {
		select {
		case <-l.stopChan:
			return
		default:
		}

		n, _, err := l.conn.ReadFrom(buf)
		if err != nil {
			select {
			case <-l.stopChan:
				return
			default:
				log.Printf("[UDP] Read error: %v", err)
				time.Sleep(100 * time.Millisecond)
				continue
			}
		}

		if n > 0 {
			l.processPacket(buf[:n])
		}
	}
}

// janitorLoop is the single place that expires soft state: bots that were not
// refreshed by a snapshot, incomplete cycles, and the online/offline edge.
// It also re-broadcasts the roster when eviction changed it.
func (l *Listener) janitorLoop() {
	ticker := time.NewTicker(2 * time.Second)
	defer ticker.Stop()

	lastOnline := l.store.IsOnline()
	for {
		select {
		case <-l.stopChan:
			return
		case <-ticker.C:
			removed := l.store.Evict()
			online := l.store.IsOnline()

			if removed {
				snap := l.store.Snapshot()
				l.metricsReg.RecordIssues(snap.Issues)
				l.hub.Broadcast("snapshot", snap)
			}
			if online != lastOnline {
				lastOnline = online
				l.hub.Broadcast("status", l.store.Status())
			}
		}
	}
}

// publishSnapshot records metrics (including issue counts) and broadcasts the
// coherent roster+issues snapshot.
func (l *Listener) publishSnapshot() {
	snap := l.store.Snapshot()
	l.metricsReg.RecordSnapshot()
	l.metricsReg.RecordIssues(snap.Issues)
	l.hub.Broadcast("snapshot", snap)
}

func (l *Listener) processPacket(data []byte) {
	var header struct {
		V    int    `json:"v"`
		Type string `json:"type"`
	}
	if err := json.Unmarshal(data, &header); err != nil {
		return
	}

	switch header.Type {
	case "HEARTBEAT":
		var hb model.HeartbeatPayload
		if err := json.Unmarshal(data, &hb); err != nil {
			return
		}

		if l.store.ApplyHeartbeat(&hb) {
			l.publishSnapshot()
		}
		l.metricsReg.RecordHeartbeat(&hb)
		l.hub.Broadcast("heartbeat", hb)

	case "BOT_BATCH":
		var batch model.BotBatchPayload
		if err := json.Unmarshal(data, &batch); err != nil {
			return
		}

		for i := range batch.Bots {
			l.project(&batch.Bots[i])
		}

		if l.store.ApplyBatch(&batch) {
			l.publishSnapshot()
		}

	default:
		var anomaly model.AnomalyPayload
		if err := json.Unmarshal(data, &anomaly); err != nil {
			return
		}
		if !model.AcceptedAnomalyTypes[anomaly.Type] {
			return
		}

		saved := l.store.AddAnomaly(anomaly)
		l.metricsReg.RecordAnomaly(&saved)
		l.hub.Broadcast("anomaly", saved)
	}
}

func (l *Listener) project(bot *model.BotSnapshot) {
	px, py, ok := l.zoneProj.Project(bot.MapID, bot.ZoneID, bot.X, bot.Y)
	bot.Projected = ok
	if ok {
		bot.PctX = px
		bot.PctY = py
	}
}
