package state

import (
	"time"

	"tortoise-observability/internal/model"
)

// anomalyActionTypes are the anomaly types that get a per-action breakdown
// in the cumulative totals: the ones whose last_action names the failing
// behaviour (ACTION_LOOP / UNREACHABLE_TARGET rows in the KPI report). Every
// other accepted type is counted per type only, so the maps stay bounded.
var anomalyActionTypes = map[string]bool{
	"ACTION_LOOP":        true,
	"UNREACHABLE_TARGET": true,
}

// countAnomalyLocked folds one anomaly into the cumulative session totals.
// Caller holds s.mu. Types outside the accepted set are ignored so the maps
// stay bounded by it, mirroring the Prometheus counter.
func (s *Store) countAnomalyLocked(a *model.AnomalyPayload, now time.Time) {
	if a == nil || !model.AcceptedAnomalyTypes[a.Type] {
		return
	}
	if s.anomSince.IsZero() {
		s.anomSince = now
	}
	s.anomByType[a.Type]++
	if !anomalyActionTypes[a.Type] {
		return
	}
	action := a.LastAction
	if action == "" {
		action = "?"
	}
	m := s.anomByAction[a.Type]
	if m == nil {
		m = make(map[string]uint64)
		s.anomByAction[a.Type] = m
	}
	m[action]++
}

// AnomalyTotals returns the cumulative anomaly counts for the current
// game-server session. The incident feed (/api/v1/anomalies) is a rolling
// 1000-row window, so whole-run counts live here instead. Since/SinceStr
// mark the window the counts cover (the session change or roster wipe that
// reset them, or the first counted anomaly). Maps are copied, so callers can
// marshal without holding the lock.
func (s *Store) AnomalyTotals() model.AnomalyTotals {
	s.mu.RLock()
	defer s.mu.RUnlock()

	out := model.AnomalyTotals{
		Totals:   make(map[string]uint64, len(s.anomByType)),
		ByAction: make(map[string]map[string]uint64, len(s.anomByAction)),
	}
	if !s.anomSince.IsZero() {
		out.Since = s.anomSince.Unix()
		out.SinceStr = stampTime(out.Since)
	}
	for k, v := range s.anomByType {
		out.Totals[k] = v
	}
	for typ, m := range s.anomByAction {
		cp := make(map[string]uint64, len(m))
		for action, n := range m {
			cp[action] = n
		}
		out.ByAction[typ] = cp
	}
	return out
}

// resetAnomalyTotalsLocked drops the cumulative anomaly counts. Called with
// the session: a new game-server process, or a roster wipe, invalidates them
// — exactly like the activity counters. Clearing the incident feed (DELETE
// /api/v1/anomalies) intentionally does not reset them: the totals are the
// whole-run record that survives the rolling window. The start time is left
// zero and stamped by the first counted anomaly instead.
func (s *Store) resetAnomalyTotalsLocked() {
	s.anomSince = time.Time{}
	s.anomByType = make(map[string]uint64)
	s.anomByAction = make(map[string]map[string]uint64)
}
