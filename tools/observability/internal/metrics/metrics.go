package metrics

import (
	"sync"
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promauto"

	"tortoise-observability/internal/model"
)

type Registry struct {
	mu             sync.RWMutex
	lastHeartbeat  time.Time
	serverOnline   prometheus.Gauge
	tickDuration   prometheus.Gauge
	playersOnline  prometheus.Gauge
	botActiveCount *prometheus.GaugeVec
	anomaliesTotal *prometheus.CounterVec
	stateRatio     *prometheus.GaugeVec
}

func New() *Registry {
	r := &Registry{
		serverOnline: promauto.NewGauge(prometheus.GaugeOpts{
			Name: "mangos_server_online",
			Help: "World server online status (1=up, 0=down)",
		}),
		tickDuration: promauto.NewGauge(prometheus.GaugeOpts{
			Name: "mangos_server_tick_duration_ms",
			Help: "Current world update tick time in milliseconds",
		}),
		playersOnline: promauto.NewGauge(prometheus.GaugeOpts{
			Name: "mangos_players_online",
			Help: "Connected human player count",
		}),
		botActiveCount: promauto.NewGaugeVec(prometheus.GaugeOpts{
			Name: "tortoisebots_active_count",
			Help: "Active bots grouped by role and class",
		}, []string{"class", "role"}),
		anomaliesTotal: promauto.NewCounterVec(prometheus.CounterOpts{
			Name: "tortoisebots_anomalies_total",
			Help: "Cumulative counter of detected bot anomalies",
		}, []string{"type"}),
		stateRatio: promauto.NewGaugeVec(prometheus.GaugeOpts{
			Name: "tortoisebots_state_ratio",
			Help: "Ratio of time spent in macro states (0.0 - 1.0)",
		}, []string{"state"}),
	}

	// Initialize server as offline until first pulse
	r.serverOnline.Set(0)

	// Background ticker to mark server offline if pulse stops for > 10 seconds
	go func() {
		ticker := time.NewTicker(2 * time.Second)
		for range ticker.C {
			r.mu.RLock()
			last := r.lastHeartbeat
			r.mu.RUnlock()

			if !last.IsZero() && time.Since(last) > 10*time.Second {
				r.serverOnline.Set(0)
			}
		}
	}()

	return r
}

func (r *Registry) RecordHeartbeat(p *model.HeartbeatPayload) {
	r.mu.Lock()
	r.lastHeartbeat = time.Now()
	r.mu.Unlock()

	r.serverOnline.Set(1)
	r.tickDuration.Set(p.TickDiffMs)
	r.playersOnline.Set(float64(p.HumansCount))

	// Update macro-state ratios
	r.stateRatio.WithLabelValues("combat").Set(p.States.Combat)
	r.stateRatio.WithLabelValues("moving").Set(p.States.Moving)
	r.stateRatio.WithLabelValues("resting").Set(p.States.Resting)
	r.stateRatio.WithLabelValues("dead").Set(p.States.Dead)
	r.stateRatio.WithLabelValues("idle").Set(p.States.Idle)

	// Reset active bot counts and re-tally
	r.botActiveCount.Reset()
	if len(p.Counts) > 0 {
		for _, c := range p.Counts {
			r.botActiveCount.WithLabelValues(c.Class, c.Role).Set(float64(c.Count))
		}
	} else {
		type countKey struct {
			class, role string
		}
		counts := make(map[countKey]float64)
		for _, b := range p.BotList {
			key := countKey{class: b.Class, role: b.Role}
			counts[key]++
		}
		for k, v := range counts {
			r.botActiveCount.WithLabelValues(k.class, k.role).Set(v)
		}
	}
}

func (r *Registry) RecordAnomaly(a *model.AnomalyPayload) {
	r.anomaliesTotal.WithLabelValues(a.Type).Inc()
}

func (r *Registry) IsOnline() bool {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return !r.lastHeartbeat.IsZero() && time.Since(r.lastHeartbeat) <= 10*time.Second
}
