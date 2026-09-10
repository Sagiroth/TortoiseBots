package state

import (
	"testing"
	"time"

	"tortoise-observability/internal/model"
)

func newTestTracker() *issueTracker { return newIssueTracker(0) }

func movingBot(x float64) model.BotSnapshot {
	return model.BotSnapshot{
		Name: "Tester", GUID: 1, Class: "warrior", Role: "dps",
		MapID: 0, ZoneID: 12, X: x, Y: 0, State: "moving",
		LastAction: "reach melee", LastTrigger: "chase",
	}
}

func anomalyBot(action string) model.BotSnapshot {
	return model.BotSnapshot{
		Name: "Looper", GUID: 7, Class: "mage", Role: "dps",
		MapID: 0, ZoneID: 12, X: 0, Y: 0, State: "combat", LastAction: action,
	}
}

func TestStuckEpisodeLifecycle(t *testing.T) {
	tr := newTestTracker()
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
	tr := newTestTracker()
	base := time.Date(2026, 9, 10, 12, 0, 0, 0, time.UTC)

	tr.Observe([]model.BotSnapshot{movingBot(0)}, base)
	tr.Observe([]model.BotSnapshot{movingBot(0)}, base.Add(11*time.Minute))

	active := tr.Snapshot().Active
	if len(active) != 1 || active[0].Severity != "persistent" {
		t.Fatalf("expected persistent severity after 11m: %+v", active)
	}
}

func TestDeadLongEpisode(t *testing.T) {
	tr := newTestTracker()
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
	tr := newTestTracker()
	base := time.Date(2026, 9, 10, 12, 0, 0, 0, time.UTC)

	tr.TouchAnomaly(model.AnomalyPayload{
		Type: "ACTION_LOOP", GUID: 7, Bot: "Looper", Class: "mage", Level: 10,
		ZoneID: 12, LastAction: "fireball", Target: "Boar",
	}, base)
	if got := tr.Snapshot().CountsByType["ACTION_LOOP"]; got != 1 {
		t.Fatalf("action loop episode not opened: %d", got)
	}

	// Within TTL, still doing the same action -> stays open.
	tr.Observe([]model.BotSnapshot{anomalyBot("fireball")}, base.Add(30*time.Second))
	if got := len(tr.Snapshot().Active); got != 1 {
		t.Fatalf("action loop episode expired too early: %d", got)
	}

	// Past TTL it closes.
	tr.Observe([]model.BotSnapshot{anomalyBot("fireball")}, base.Add(3*time.Minute))
	snap := tr.Snapshot()
	if len(snap.Active) != 0 || len(snap.Resolved) != 1 {
		t.Fatalf("action loop episode did not expire: %+v", snap)
	}
}

func TestContradictedUnreachableClosesEarly(t *testing.T) {
	tr := newTestTracker()
	base := time.Date(2026, 9, 10, 12, 0, 0, 0, time.UTC)

	tr.TouchAnomaly(model.AnomalyPayload{
		Type: "UNREACHABLE_TARGET", GUID: 7, Bot: "Looper", Class: "mage", Level: 10,
		ZoneID: 12, Target: "Boar",
	}, base)

	// Bot is no longer in combat, so the unreachable episode is over even
	// though the TTL has not elapsed.
	free := anomalyBot("")
	free.State = "idle"
	free.Target = ""
	tr.Observe([]model.BotSnapshot{free}, base.Add(30*time.Second))
	if got := len(tr.Snapshot().Active); got != 0 {
		t.Fatalf("contradicted unreachable episode stayed open: %d", got)
	}
}

func TestUnreachableStaysWhileRefreshed(t *testing.T) {
	tr := newTestTracker()
	base := time.Date(2026, 9, 10, 12, 0, 0, 0, time.UTC)

	tr.TouchAnomaly(model.AnomalyPayload{
		Type: "UNREACHABLE_TARGET", GUID: 7, Bot: "Looper", Class: "mage", Level: 10,
		ZoneID: 12, Target: "Boar",
	}, base)
	// Emitter re-reports while the target is still unreachable.
	tr.TouchAnomaly(model.AnomalyPayload{
		Type: "UNREACHABLE_TARGET", GUID: 7, Bot: "Looper", Class: "mage", Level: 10,
		ZoneID: 12, Target: "Boar",
	}, base.Add(90*time.Second))

	fighting := anomalyBot("fireball")
	fighting.Target = "Boar"
	tr.Observe([]model.BotSnapshot{fighting}, base.Add(150*time.Second))
	if got := len(tr.Snapshot().Active); got != 1 {
		t.Fatalf("refreshed unreachable episode did not stay open: %d", got)
	}
}

func TestMinIssueAgeHidesShortEpisodes(t *testing.T) {
	tr := newIssueTracker(5 * time.Minute)
	base := time.Date(2026, 9, 10, 12, 0, 0, 0, time.UTC)

	// Stuck for 2 minutes: tracked internally but not surfaced.
	tr.Observe([]model.BotSnapshot{movingBot(0)}, base)
	tr.Observe([]model.BotSnapshot{movingBot(0)}, base.Add(2*time.Minute))
	if got := len(tr.Snapshot().Active); got != 0 {
		t.Fatalf("short episode surfaced: %d", got)
	}

	// Still stuck at 6 minutes: now surfaced.
	tr.Observe([]model.BotSnapshot{movingBot(0)}, base.Add(6*time.Minute))
	if got := len(tr.Snapshot().Active); got != 1 {
		t.Fatalf("long episode hidden: %d", got)
	}

	// A short episode that clears before the minimum age is discarded, not
	// archived as resolved.
	short := newIssueTracker(5 * time.Minute)
	short.Observe([]model.BotSnapshot{movingBot(0)}, base)
	short.Observe([]model.BotSnapshot{movingBot(0)}, base.Add(2*time.Minute))
	short.Observe([]model.BotSnapshot{movingBot(100)}, base.Add(2*time.Minute+time.Second))
	if got := len(short.Snapshot().Resolved); got != 0 {
		t.Fatalf("short episode was archived: %d", got)
	}
}

func TestTrackerResetKeepsResolved(t *testing.T) {
	tr := newTestTracker()
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
