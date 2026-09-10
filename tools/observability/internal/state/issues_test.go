package state

import (
	"testing"
	"time"

	"tortoise-observability/internal/model"
)

func movingBot(x float64) model.BotSnapshot {
	return model.BotSnapshot{
		Name: "Tester", GUID: 1, Class: "warrior", Role: "dps",
		MapID: 0, ZoneID: 12, X: x, Y: 0, State: "moving",
		LastAction: "reach melee", LastTrigger: "chase",
	}
}

func TestStuckEpisodeLifecycle(t *testing.T) {
	tr := newIssueTracker()
	base := time.Date(2026, 9, 10, 12, 0, 0, 0, time.UTC)

	tr.Observe([]model.BotSnapshot{movingBot(0)}, base)
	tr.Observe([]model.BotSnapshot{movingBot(0)}, base.Add(30*time.Second))
	if got := len(tr.Snapshot().Active); got != 0 {
		t.Fatalf("stuck issue opened before threshold: %d", got)
	}

	tr.Observe([]model.BotSnapshot{movingBot(0)}, base.Add(61*time.Second))
	active := tr.Snapshot().Active
	if len(active) != 1 || active[0].Type != "STUCK" || active[0].Severity != "watch" {
		t.Fatalf("stuck issue not opened: %+v", active)
	}
	if active[0].Action != "reach melee" || active[0].Trigger != "chase" {
		t.Fatalf("stuck issue missing action context: %+v", active[0])
	}

	// Moving again closes the episode and archives it.
	tr.Observe([]model.BotSnapshot{movingBot(100)}, base.Add(70*time.Second))
	snap := tr.Snapshot()
	if len(snap.Active) != 0 {
		t.Fatalf("stuck issue stayed open after movement: %+v", snap.Active)
	}
	if len(snap.Resolved) != 1 || snap.Resolved[0].Type != "STUCK" {
		t.Fatalf("stuck episode not resolved: %+v", snap.Resolved)
	}
}

func TestPersistentSeverity(t *testing.T) {
	tr := newIssueTracker()
	base := time.Date(2026, 9, 10, 12, 0, 0, 0, time.UTC)

	tr.Observe([]model.BotSnapshot{movingBot(0)}, base)
	tr.Observe([]model.BotSnapshot{movingBot(0)}, base.Add(11*time.Minute))

	active := tr.Snapshot().Active
	if len(active) != 1 || active[0].Severity != "persistent" {
		t.Fatalf("expected persistent severity after 11m: %+v", active)
	}
}

func TestDeadLongEpisode(t *testing.T) {
	tr := newIssueTracker()
	base := time.Date(2026, 9, 10, 12, 0, 0, 0, time.UTC)

	dead := movingBot(0)
	dead.State = "dead"
	tr.Observe([]model.BotSnapshot{dead}, base)
	tr.Observe([]model.BotSnapshot{dead}, base.Add(2*time.Minute+time.Second))

	active := tr.Snapshot().Active
	if len(active) != 1 || active[0].Type != "DEAD_LONG" {
		t.Fatalf("dead episode not detected: %+v", active)
	}
}

func TestAnomalyEpisodeExpires(t *testing.T) {
	tr := newIssueTracker()
	base := time.Date(2026, 9, 10, 12, 0, 0, 0, time.UTC)

	tr.TouchAnomaly(model.AnomalyPayload{
		Type: "ACTION_LOOP", GUID: 7, Bot: "Looper", Class: "mage", Level: 10,
		ZoneID: 12, LastAction: "fireball", Target: "Boar",
	}, base)
	if got := tr.Snapshot().CountsByType["ACTION_LOOP"]; got != 1 {
		t.Fatalf("action loop episode not opened: %d", got)
	}

	// Within TTL it stays open even with no fresh snapshots.
	tr.Observe(nil, base.Add(30*time.Second))
	if got := len(tr.Snapshot().Active); got != 1 {
		t.Fatalf("action loop episode expired too early: %d", got)
	}

	// Past TTL it closes.
	tr.Observe(nil, base.Add(3*time.Minute))
	snap := tr.Snapshot()
	if len(snap.Active) != 0 || len(snap.Resolved) != 1 {
		t.Fatalf("action loop episode did not expire: %+v", snap)
	}
}

func TestTrackerResetKeepsResolved(t *testing.T) {
	tr := newIssueTracker()
	base := time.Date(2026, 9, 10, 12, 0, 0, 0, time.UTC)

	// Open a stuck issue, then clear it by moving again.
	tr.Observe([]model.BotSnapshot{movingBot(0)}, base)
	tr.Observe([]model.BotSnapshot{movingBot(0)}, base.Add(2*time.Minute))
	tr.Observe([]model.BotSnapshot{movingBot(100)}, base.Add(2*time.Minute+time.Second))
	if len(tr.Snapshot().Resolved) != 1 {
		t.Fatal("expected one resolved issue")
	}

	// Re-open one, then reset (simulating a server restart).
	tr.Observe([]model.BotSnapshot{movingBot(0)}, base.Add(3*time.Minute))
	tr.Observe([]model.BotSnapshot{movingBot(0)}, base.Add(4*time.Minute))
	tr.Reset()

	snap := tr.Snapshot()
	if len(snap.Active) != 0 {
		t.Fatalf("reset did not clear active issues: %d", len(snap.Active))
	}
	if len(snap.Resolved) != 1 {
		t.Fatalf("reset wiped resolved history: %d", len(snap.Resolved))
	}
}
