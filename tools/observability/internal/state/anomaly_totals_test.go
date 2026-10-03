package state

import (
	"testing"
	"time"

	"tortoise-observability/internal/model"
)
func anomalyOf(typ, action string) model.AnomalyPayload {
	return model.AnomalyPayload{
		Type: typ, LastAction: action, Bot: "Tester", GUID: 7,
		Severity: "WARN",
	}
}

func TestAnomalyTotalsCountAllTypes(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.AddAnomaly(anomalyOf("ACTION_LOOP", "move"))
	c.Advance(0) // same instant: totals must not split
	s.AddAnomaly(anomalyOf("ACTION_LOOP", "move"))
	s.AddAnomaly(anomalyOf("UNREACHABLE_TARGET", "combat reach"))
	s.AddAnomaly(anomalyOf("BOT_DEATH", ""))
	s.AddAnomaly(anomalyOf("STUCK", "move")) // counter-only, never buffered

	got := s.AnomalyTotals()
	if got.Totals["ACTION_LOOP"] != 2 {
		t.Fatalf("ACTION_LOOP = %d, want 2: %+v", got.Totals["ACTION_LOOP"], got.Totals)
	}
	if got.Totals["UNREACHABLE_TARGET"] != 1 || got.Totals["BOT_DEATH"] != 1 {
		t.Fatalf("totals wrong: %+v", got.Totals)
	}
	if got.Totals["STUCK"] != 1 {
		t.Fatalf("counter-only STUCK must still be counted: %+v", got.Totals)
	}
	if got.ByAction["ACTION_LOOP"]["move"] != 2 {
		t.Fatalf("ACTION_LOOP/move wrong: %+v", got.ByAction)
	}
	if got.ByAction["UNREACHABLE_TARGET"]["combat reach"] != 1 {
		t.Fatalf("UNREACHABLE breakdown wrong: %+v", got.ByAction)
	}
	if _, ok := got.ByAction["BOT_DEATH"]; ok {
		t.Fatalf("BOT_DEATH must have no per-action breakdown: %+v", got.ByAction)
	}
	if got.Since == 0 || got.SinceStr == "" {
		t.Fatalf("totals must expose the window start: %+v", got)
	}
	if s.Anomalies().Count() != 4 {
		t.Fatalf("ring buffer must hold every buffered type (STUCK is the only counter-only one), got %d", s.Anomalies().Count())
	}
}

func TestAnomalyTotalsBlankAndEmptyActions(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.AddAnomaly(anomalyOf("ACTION_LOOP", ""))
	got := s.AnomalyTotals()
	if got.ByAction["ACTION_LOOP"]["?"] != 1 {
		t.Fatalf("blank last_action must bucket under '?': %+v", got.ByAction)
	}
}

func TestAnomalyTotalsIgnoreUnknownTypes(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.AddAnomaly(anomalyOf("BOGUS_NOISE", "move"))
	got := s.AnomalyTotals()
	if len(got.Totals) != 0 {
		t.Fatalf("unknown types must not be counted: %+v", got.Totals)
	}
}

func TestAnomalyTotalsResetOnSessionChange(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.AddAnomaly(anomalyOf("BOT_DEATH", ""))
	if got := s.AnomalyTotals().Totals["BOT_DEATH"]; got != 1 {
		t.Fatalf("setup BOT_DEATH = %d, want 1", got)
	}

	hb := heartbeat(1, 1)
	hb.Session = 42
	s.ApplyHeartbeat(hb)
	c.Advance(0)

	got := s.AnomalyTotals()
	if len(got.Totals) != 0 || len(got.ByAction) != 0 {
		t.Fatalf("session change must reset totals: %+v", got)
	}

	s.AddAnomaly(anomalyOf("ACTION_LOOP", "move"))
	if got := s.AnomalyTotals().Totals["ACTION_LOOP"]; got != 1 {
		t.Fatalf("post-reset ACTION_LOOP = %d, want 1", got)
	}
}

func TestAnomalyTotalsResetOnRosterWipe(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyHeartbeat(heartbeat(1, 1))
	s.ApplyBatch(batch(1, 0, 1, bot(1, "Alpha", 0)))
	s.AddAnomaly(anomalyOf("BOT_DEATH", ""))
	if got := s.AnomalyTotals().Totals["BOT_DEATH"]; got != 1 {
		t.Fatalf("setup BOT_DEATH = %d, want 1", got)
	}

	c.Advance(35 * time.Second) // past RosterTTL
	if !s.Evict() {
		t.Fatal("roster should have been wiped")
	}
	if got := s.AnomalyTotals(); len(got.Totals) != 0 {
		t.Fatalf("roster wipe must reset totals: %+v", got)
	}
}

func TestAnomalyTotalsSurviveFeedClear(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.AddAnomaly(anomalyOf("ACTION_LOOP", "move"))
	s.Anomalies().Clear() // the dashboard's Clear button: window only

	if got := s.AnomalyTotals().Totals["ACTION_LOOP"]; got != 1 {
		t.Fatalf("Clear must not reset totals: got %d", got)
	}
}
