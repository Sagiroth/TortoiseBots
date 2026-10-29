// Package udp ingests telemetry datagrams from the game server and feeds the
// authoritative state store. Ingestion never blocks on WebSocket clients.
package udp

import (
	"encoding/json"
	"fmt"
	"log"
	"net"
	"time"
	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/history"
	zoneproject "tortoise-observability/internal/map"
	"tortoise-observability/internal/metrics"
	"tortoise-observability/internal/model"
	"tortoise-observability/internal/state"
)

// Hub is the broadcast surface the listener needs.
type Hub interface {
	Broadcast(event string, payload interface{})
}

// LiveSink is the realtime fan-out (the SSE hub): positions and fight events,
// filtered per commander. It never blocks the read loop.
type LiveSink interface {
	Publish(commander, event string, payload interface{})
}

// Listener owns the UDP socket and the janitor that evicts stale state.
// Combat, when set, receives the module's COMBAT_FIGHT datagrams; nil keeps
// the listener's old shape (an older module sends none and nothing stores).
type Listener struct {
	addr       string
	conn       *net.UDPConn
	store      *state.Store
	combat     *combat.Store
	metricsReg *metrics.Registry
	zoneProj   *zoneproject.Engine
	hub        Hub
	live       LiveSink
	history    HistorySink
	forwards   []*forwardTarget
	stopChan   chan struct{}
}

// SetCombatStore wires the fight store the COMBAT_FIGHT case files into.
func (l *Listener) SetCombatStore(c *combat.Store) { l.combat = c }

// SetLiveSink wires the realtime stream (COMMANDERS, COMMANDER_POS and the
// fight events). Nil keeps the listener's old shape: nothing is pushed there.
func (l *Listener) SetLiveSink(s LiveSink) { l.live = s }

// HistorySink takes a finished fight for the on-disk history; Enqueue never blocks. Nil keeps the listener's old shape.
type HistorySink interface {
	Enqueue(history.Item) bool
}

// SetHistory wires the fight history. It is set before Start, like the combat store.
func (l *Listener) SetHistory(h HistorySink) { l.history = h }

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
	l.closeForwards()
}

func (l *Listener) readLoop() {
	buf := make([]byte, 65535)

	for {
		select {
		case <-l.stopChan:
			return
		default:
		}

		n, src, err := l.conn.ReadFromUDP(buf)
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
			// A datagram from one of our own forward sockets went round a loop:
			// drop it before it is forwarded or processed a second time.
			if l.isOwnForward(src) {
				continue
			}
			// Forward first: the copy goes out whatever the parse makes of it.
			l.forwardDatagram(buf[:n])
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
				l.metricsReg.RecordGrinding(snap.Grinding)
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
	l.metricsReg.RecordGrinding(snap.Grinding)
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
			// Issue #872: a pre-v11 module sends no kind; read it off the
			// flags it does send so old and new servers render the same.
			if batch.Bots[i].Kind == "" {
				batch.Bots[i].Kind = model.KindFromFlags(batch.Bots[i].Player, batch.Bots[i].Alt)
			}
		}

		if l.store.ApplyBatch(&batch) {
			l.publishSnapshot()
		}

	case "BOT_EVENTS":
		var events model.BotEventsPayload
		if err := json.Unmarshal(data, &events); err != nil {
			return
		}
		l.store.ApplyBotEvents(&events)

	case "SERVER_INFO":
		var info model.ServerInfoPayload
		if err := json.Unmarshal(data, &info); err != nil {
			return
		}
		l.store.ApplyServerInfo(&info)
		// Broadcast alone (no roster republish): the panel listens for this
		// event and the next snapshot carries the same payload inline.
		l.hub.Broadcast("server_info", info)

	case "DUNGEON_MOBS":
		var dm model.DungeonMobsPayload
		if err := json.Unmarshal(data, &dm); err != nil {
			return
		}
		// No stream event: the browser refetches /api/v1/dungeons on the
		// snapshot broadcast, and the module sends this before the batches.
		l.store.ApplyDungeonMobs(&dm)

	case "COMMANDERS":
		var cmd model.CommandersPayload
		if err := json.Unmarshal(data, &cmd); err != nil {
			return
		}
		if l.store.ApplyCommanders(&cmd) {
			l.hub.Broadcast("commanders", l.store.Commanders())
			if l.live != nil {
				tsMs := cmd.TSMs
				if tsMs == 0 {
					tsMs = cmd.TS * 1000 // a module before ts_ms (2.3 is additive)
				}
				for i := range cmd.Commanders {
					b := cmd.Commanders[i]
					l.live.Publish(b.Commander, "commander", struct {
						model.CommanderBinding
						TSMs int64 `json:"ts_ms"`
					}{b, tsMs})
				}
			}
		}

	case "COMBAT_FIGHT":
		if l.combat == nil {
			return
		}
		var dg combat.FightDatagram
		if err := json.Unmarshal(data, &dg); err != nil {
			return
		}
		if dg.Fight.ID == "" {
			return
		}
		if summary, ok := l.combat.AddSummary(dg.Session, dg.Fight); ok {
			l.hub.Broadcast("combat", dg.Fight)
			if l.live != nil {
				l.live.Publish(dg.Fight.Commander, "fight", summary)
			}
			if l.history != nil {
				l.history.Enqueue(history.Item{Session: dg.Session, Fight: dg.Fight, Warnings: summary.Warnings, At: time.Now()})
			}
		}

	case "COMBAT_EVENTS":
		var dg combat.EventsDatagram
		if err := json.Unmarshal(data, &dg); err != nil {
			return
		}
		l.metricsReg.RecordRealtimeDatagram("COMBAT_EVENTS", len(data))
		if l.combat == nil {
			return
		}
		summary, changed, accepted := l.combat.ApplyEvents(dg, time.Now())
		if !accepted || l.live == nil {
			return
		}
		l.live.Publish(dg.Commander, "events", dg)
		if changed {
			l.live.Publish(dg.Commander, "fight_live", summary)
		}

	case "COMMANDER_POS":
		var p model.CommanderPosPayload
		if err := json.Unmarshal(data, &p); err != nil {
			return
		}
		l.metricsReg.RecordRealtimeDatagram("COMMANDER_POS", len(data))
		if frame, ok := l.store.ApplyCommanderPos(&p); ok && l.live != nil {
			l.live.Publish(p.Commander, "pos", frame)
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
		if anomaly.Type == "STUCK" {
			// Counter-only: no incident row, no live push. The 60 s STUCK
			// issue episode is the surfaced signal.
			return
		}
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
