package metrics

import (
	"sync"
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promauto"

	"tortoise-observability/internal/model"
)

// Registry exposes Prometheus collectors fed from UDP telemetry.
type Registry struct {
	mu            sync.RWMutex
	lastHeartbeat time.Time
	online        bool

	serverOnline   prometheus.Gauge
	tickDuration   prometheus.Gauge
	playersOnline  prometheus.Gauge
	botActiveCount *prometheus.GaugeVec
	anomaliesTotal *prometheus.CounterVec
	stateRatio     *prometheus.GaugeVec
	issuesActive   *prometheus.GaugeVec
	snapshotsTotal prometheus.Counter
	// Grinding panel: pool-wide XP and kill rates derived per snapshot.
	grindingXpHour  prometheus.Gauge
	grindingGaining prometheus.Gauge
	grindingKills   prometheus.Gauge
	grindingCombat  prometheus.Gauge
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
			Help: "Rolling-window ratio of time spent in macro states (0.0 - 1.0)",
		}, []string{"state"}),
		issuesActive: promauto.NewGaugeVec(prometheus.GaugeOpts{
			Name: "tortoisebots_issues_active",
			Help: "Active bot issue episodes grouped by type",
		}, []string{"type"}),
		snapshotsTotal: promauto.NewCounter(prometheus.CounterOpts{
			Name: "tortoisebots_snapshots_total",
			Help: "Complete bot roster snapshots published by the daemon",
		}),
		grindingXpHour: promauto.NewGauge(prometheus.GaugeOpts{
			Name: "tortoisebots_grinding_xp_per_hour_total",
			Help: "Pool-wide XP per hour summed over bots with a positive windowed rate",
		}),
		grindingGaining: promauto.NewGauge(prometheus.GaugeOpts{
			Name: "tortoisebots_grinding_bots_gaining",
			Help: "Bots with an XP gain in the last 10 minutes",
		}),
		grindingKills: promauto.NewGauge(prometheus.GaugeOpts{
			Name: "tortoisebots_grinding_deaths_per_min",
			Help: "BOT_DEATH bot deaths per minute over the last 10 minutes",
		}),
		grindingCombat: promauto.NewGauge(prometheus.GaugeOpts{
			Name: "tortoisebots_grinding_pct_in_combat",
			Help: "Percent of the roster in combat state (0-100)",
		}),
	}

	// Initialize server as offline until the first pulse.
	r.serverOnline.Set(0)

	// Flip the server offline (and zero out gauges that describe live world
	// state) when pulses stop, so dashboards and alerts never show a frozen
	// picture as if it were current.
	go func() {
		ticker := time.NewTicker(2 * time.Second)
		for range ticker.C {
			if !r.IsOnline() {
				r.markOffline()
			}
		}
	}()

	return r
}

func (r *Registry) RecordHeartbeat(p *model.HeartbeatPayload) {
	r.mu.Lock()
	r.lastHeartbeat = time.Now()
	r.online = true
	r.mu.Unlock()

	r.serverOnline.Set(1)
	r.tickDuration.Set(p.TickDiffMs)
	r.playersOnline.Set(float64(p.HumansCount))

	r.stateRatio.WithLabelValues("combat").Set(p.States.Combat)
	r.stateRatio.WithLabelValues("moving").Set(p.States.Moving)
	r.stateRatio.WithLabelValues("busy").Set(p.States.Busy)
	r.stateRatio.WithLabelValues("resting").Set(p.States.Resting)
	r.stateRatio.WithLabelValues("dead").Set(p.States.Dead)
	r.stateRatio.WithLabelValues("idle").Set(p.States.Idle)

	// Reset active bot counts and re-tally.
	r.botActiveCount.Reset()
	if len(p.Counts) > 0 {
		for _, c := range p.Counts {
			r.botActiveCount.WithLabelValues(c.Class, c.Role).Set(float64(c.Count))
		}
	}
}

func (r *Registry) RecordAnomaly(a *model.AnomalyPayload) {
	if !model.AcceptedAnomalyTypes[a.Type] {
		return
	}
	r.anomaliesTotal.WithLabelValues(a.Type).Inc()
}

func (r *Registry) RecordSnapshot() {
	r.snapshotsTotal.Inc()
}

// RecordGrinding mirrors the dashboard grinding panel so alerts can use it.
func (r *Registry) RecordGrinding(g model.GrindingSummary) {
	r.grindingXpHour.Set(g.TotalXpHour)
	r.grindingGaining.Set(float64(g.BotsGainingXP))
	r.grindingKills.Set(g.DeathsPerMin)
	if n := g.BotsTracked; n > 0 {
		r.grindingCombat.Set(float64(g.StateCounts["combat"]) / float64(n) * 100)
	} else {
		r.grindingCombat.Set(0)
	}
}

func (r *Registry) RecordIssues(snap model.IssueSnapshot) {
	r.issuesActive.Reset()
	for typ, n := range snap.CountsByType {
		r.issuesActive.WithLabelValues(typ).Set(float64(n))
	}
}

func (r *Registry) IsOnline() bool {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.online && !r.lastHeartbeat.IsZero() && time.Since(r.lastHeartbeat) <= 10*time.Second
}

func (r *Registry) markOffline() {
	r.mu.Lock()
	wasOnline := r.online
	r.online = false
	r.mu.Unlock()

	if !wasOnline {
		return
	}

	r.serverOnline.Set(0)
	r.tickDuration.Set(0)
	r.playersOnline.Set(0)
	r.botActiveCount.Reset()
	r.issuesActive.Reset()
	r.grindingXpHour.Set(0)
	r.grindingGaining.Set(0)
	r.grindingKills.Set(0)
	r.grindingCombat.Set(0)
	for _, state := range []string{"combat", "moving", "busy", "resting", "dead", "idle"} {
		r.stateRatio.WithLabelValues(state).Set(0)
	}
}
