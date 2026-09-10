package udp

import (
	"encoding/json"
	"fmt"
	"log"
	"net"
	"sync"
	"time"

	zoneproject "tortoise-observability/internal/map"
	"tortoise-observability/internal/metrics"
	"tortoise-observability/internal/model"
	"tortoise-observability/internal/ringbuf"
)

type Hub interface {
	Broadcast(msgType string, payload interface{})
}

type Listener struct {
	addr        string
	conn        *net.UDPConn
	ringBuf     *ringbuf.RingBuffer
	metricsReg  *metrics.Registry
	zoneProj    *zoneproject.Engine
	hub         Hub
	stopChan    chan struct{}

	// Live bot trails: botGUID -> []model.Coordinate (last 10 positions)
	trailMu     sync.RWMutex
	trails      map[uint32][]model.Coordinate
	lastBots    []model.BotSnapshot
	lastHeartbeat *model.HeartbeatPayload
}

func NewListener(port int, rb *ringbuf.RingBuffer, mr *metrics.Registry, zp *zoneproject.Engine, hub Hub) *Listener {
	return &Listener{
		addr:       fmt.Sprintf("0.0.0.0:%d", port),
		ringBuf:    rb,
		metricsReg: mr,
		zoneProj:   zp,
		hub:        hub,
		stopChan:   make(chan struct{}),
		trails:     make(map[uint32][]model.Coordinate),
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

	// 64 KB receive buffer
	_ = l.conn.SetReadBuffer(64 * 1024)

	log.Printf("[UDP] Listening for TortoiseBots telemetry on %s", l.addr)

	go l.readLoop()
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

		if n <= 0 {
			continue
		}

		payload := buf[:n]
		l.processPacket(payload)
	}
}

func (l *Listener) processPacket(data []byte) {
	// Quick peek for type field
	var header struct {
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

		l.trailMu.Lock()
		l.lastHeartbeat = &hb

		if len(hb.BotList) > 0 {
			for i := range hb.BotList {
				l.projectAndTrail(&hb.BotList[i])
			}
			l.lastBots = hb.BotList
		}
		l.trailMu.Unlock()

		l.metricsReg.RecordHeartbeat(&hb)

		if l.hub != nil {
			l.hub.Broadcast("heartbeat", hb)
		}

	case "BOT_BATCH":
		var batch model.BotBatchPayload
		if err := json.Unmarshal(data, &batch); err != nil {
			return
		}

		l.trailMu.Lock()
		// If first batch, replace or update
		if batch.BatchIndex == 0 {
			l.lastBots = make([]model.BotSnapshot, 0, len(batch.Bots)*batch.TotalBatches)
		}
		for i := range batch.Bots {
			bot := &batch.Bots[i]
			l.projectAndTrail(bot)
			l.lastBots = append(l.lastBots, *bot)
		}
		l.trailMu.Unlock()

		if l.hub != nil {
			l.hub.Broadcast("bots", batch.Bots)
		}

	default:
		// Anomaly event
		var anomaly model.AnomalyPayload
		if err := json.Unmarshal(data, &anomaly); err != nil {
			return
		}

		saved := l.ringBuf.Add(anomaly)
		l.metricsReg.RecordAnomaly(&saved)

		if l.hub != nil {
			l.hub.Broadcast("anomaly", saved)
		}
	}
}

func (l *Listener) projectAndTrail(bot *model.BotSnapshot) {
	if px, py, ok := l.zoneProj.Project(bot.MapID, bot.ZoneID, bot.X, bot.Y); ok {
		bot.PctX = px
		bot.PctY = py
	}

	coord := model.Coordinate{
		X:    bot.X,
		Y:    bot.Y,
		PctX: bot.PctX,
		PctY: bot.PctY,
	}
	trail := l.trails[bot.GUID]
	trail = append(trail, coord)
	if len(trail) > 10 {
		trail = trail[len(trail)-10:]
	}
	l.trails[bot.GUID] = trail
	bot.Trail = trail
}

func (l *Listener) GetLastBots() []model.BotSnapshot {
	l.trailMu.RLock()
	defer l.trailMu.RUnlock()
	res := make([]model.BotSnapshot, len(l.lastBots))
	copy(res, l.lastBots)
	return res
}

func (l *Listener) GetLastHeartbeat() *model.HeartbeatPayload {
	l.trailMu.RLock()
	defer l.trailMu.RUnlock()
	if l.lastHeartbeat == nil {
		return nil
	}
	hb := *l.lastHeartbeat
	return &hb
}
