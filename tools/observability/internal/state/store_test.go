package state

import (
	"testing"
	"time"

	"tortoise-observability/internal/model"
	"tortoise-observability/internal/ringbuf"
)

type clock struct {
	now time.Time
}

func newClock() *clock {
	return &clock{now: time.Date(2026, 9, 10, 12, 0, 0, 0, time.UTC)}
}

func (c *clock) Now() time.Time          { return c.now }
func (c *clock) Advance(d time.Duration) { c.now = c.now.Add(d) }

func newTestStore(c *clock) *Store {
	s := New(Config{
		TrailPoints: 3,
		BotTTL:      8 * time.Second,
		RosterTTL:   30 * time.Second,
		PendingTTL:  10 * time.Second,
	}, ringbuf.New(16))
	s.SetClock(c.Now)
	return s
}

func heartbeat(seq uint64, bots uint32) *model.HeartbeatPayload {
	return &model.HeartbeatPayload{
		V: model.ProtocolVersion, Seq: seq, Type: "HEARTBEAT",
		BotsCount: bots, HumansCount: 1, TickDiffMs: 50,
		States: model.StateRatios{Idle: 1},
	}
}

func batch(seq uint64, index, total int, bots ...model.BotSnapshot) *model.BotBatchPayload {
	return &model.BotBatchPayload{
		V: model.ProtocolVersion, Seq: seq, Type: "BOT_BATCH",
		BatchIndex: index, TotalBatches: total, Bots: bots,
	}
}

func bot(guid uint32, name string, x float64) model.BotSnapshot {
	return model.BotSnapshot{
		Name: name, GUID: guid, Class: "warrior", Role: "dps",
		MapID: 0, ZoneID: 12, X: x, Y: 0, State: "idle",
		Projected: true, PctX: 50, PctY: 50,
	}
}

func TestCompleteCyclePublishesRoster(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyHeartbeat(heartbeat(1, 2))
	if published := s.ApplyBatch(batch(1, 0, 2, bot(1, "Alpha", 0))); published {
		t.Fatal("cycle published before all batches arrived")
	}
	if published := s.ApplyBatch(batch(1, 1, 2, bot(2, "Beta", 0))); !published {
		t.Fatal("cycle did not publish after final batch")
	}

	snap := s.Snapshot()
	if len(snap.Bots) != 2 {
		t.Fatalf("expected 2 bots, got %d", len(snap.Bots))
	}
	if snap.Bots[0].Name != "Alpha" || snap.Bots[1].Name != "Beta" {
		t.Fatalf("roster not sorted by name: %+v", snap.Bots)
	}
}

func TestIncompleteCycleDoesNotClobberRoster(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyHeartbeat(heartbeat(1, 2))
	s.ApplyBatch(batch(1, 0, 2, bot(1, "Alpha", 0)))
	s.ApplyBatch(batch(1, 1, 2, bot(2, "Beta", 0)))

	// Cycle 2 loses its second batch, then cycle 3 begins.
	s.ApplyHeartbeat(heartbeat(2, 2))
	s.ApplyBatch(batch(2, 0, 2, bot(1, "Alpha", 1)))
	c.Advance(time.Second)
	s.ApplyHeartbeat(heartbeat(3, 1))
	s.ApplyBatch(batch(3, 0, 1, bot(3, "Gamma", 0)))

	snap := s.Snapshot()
	if len(snap.Bots) != 1 || snap.Bots[0].Name != "Gamma" {
		t.Fatalf("incomplete cycle leaked into roster: %+v", snap.Bots)
	}
	if snap.Server.Seq != 3 {
		t.Fatalf("expected seq 3, got %d", snap.Server.Seq)
	}
}

func TestDuplicateAndStaleBatchesIgnored(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyHeartbeat(heartbeat(1, 2))
	s.ApplyBatch(batch(1, 0, 2, bot(1, "Alpha", 0)))
	if s.ApplyBatch(batch(1, 0, 2, bot(1, "Alpha", 0))) {
		t.Fatal("duplicate batch published a cycle")
	}
	s.ApplyBatch(batch(1, 1, 2, bot(2, "Beta", 0)))

	// A late duplicate from the finished cycle must not replace the roster.
	s.ApplyBatch(batch(1, 0, 2, bot(9, "Old", 0)))
	s.ApplyBatch(batch(1, 1, 2, bot(8, "Old", 0)))

	snap := s.Snapshot()
	if len(snap.Bots) != 2 {
		t.Fatalf("stale cycle modified roster: %+v", snap.Bots)
	}
}

func TestEmptyRosterHeartbeatPublishes(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyHeartbeat(heartbeat(1, 1))
	s.ApplyBatch(batch(1, 0, 1, bot(1, "Alpha", 0)))

	if published := s.ApplyHeartbeat(heartbeat(2, 0)); !published {
		t.Fatal("empty roster heartbeat did not publish")
	}

	snap := s.Snapshot()
	if len(snap.Bots) != 0 {
		t.Fatalf("expected empty roster, got %d bots", len(snap.Bots))
	}
	if snap.Server.Stale {
		t.Fatal("fresh empty roster marked stale")
	}
}

func TestAuthoritativeSnapshotRemovesLoggedOutBots(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyHeartbeat(heartbeat(1, 2))
	s.ApplyBatch(batch(1, 0, 2, bot(1, "Alpha", 0), bot(2, "Beta", 0)))

	// Beta logs out: the next complete snapshot alone must drop it. No TTL
	// or external eviction should be required.
	c.Advance(3 * time.Second)
	s.ApplyHeartbeat(heartbeat(2, 1))
	s.ApplyBatch(batch(2, 0, 1, bot(1, "Alpha", 0)))

	snap := s.Snapshot()
	if len(snap.Bots) != 1 || snap.Bots[0].Name != "Alpha" {
		t.Fatalf("snapshot left departed bots in roster: %+v", snap.Bots)
	}
}

func TestRosterExpiresWhenSnapshotsStop(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyHeartbeat(heartbeat(1, 1))
	s.ApplyBatch(batch(1, 0, 1, bot(1, "Alpha", 0)))

	// Stale but still displayed: useful context while the server is offline.
	c.Advance(20 * time.Second)
	if removed := s.Evict(); removed {
		t.Fatal("roster wiped before RosterTTL elapsed")
	}
	if !s.Status().Stale {
		t.Fatal("roster not marked stale after BotTTL")
	}

	// Too old to be useful: wiped cleanly instead of haunting the dashboard.
	c.Advance(15 * time.Second)
	if removed := s.Evict(); !removed {
		t.Fatal("roster not wiped after RosterTTL")
	}
	if len(s.Snapshot().Bots) != 0 {
		t.Fatal("wiped roster still reports bots")
	}
}

func TestTrailContinuityAndZoneReset(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyHeartbeat(heartbeat(1, 1))
	s.ApplyBatch(batch(1, 0, 1, bot(1, "Alpha", 0)))

	c.Advance(2 * time.Second)
	s.ApplyHeartbeat(heartbeat(2, 1))
	s.ApplyBatch(batch(2, 0, 1, bot(1, "Alpha", 5)))

	if got := len(s.Snapshot().Bots[0].Trail); got != 2 {
		t.Fatalf("expected trail of 2 points, got %d", got)
	}

	// Stationary refresh must not add duplicate trail points.
	c.Advance(2 * time.Second)
	s.ApplyHeartbeat(heartbeat(3, 1))
	s.ApplyBatch(batch(3, 0, 1, bot(1, "Alpha", 5)))
	if got := len(s.Snapshot().Bots[0].Trail); got != 2 {
		t.Fatalf("stationary bot added duplicate trail points: %d", got)
	}

	// Zone change resets the trail so coordinates never leak across maps.
	c.Advance(2 * time.Second)
	s.ApplyHeartbeat(heartbeat(4, 1))
	moved := bot(1, "Alpha", 5)
	moved.ZoneID = 14
	s.ApplyBatch(batch(4, 0, 1, moved))
	if got := len(s.Snapshot().Bots[0].Trail); got != 1 {
		t.Fatalf("zone change did not reset trail: %d points", got)
	}
}

func TestTrailIsCapped(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	for i := 0; i < 10; i++ {
		seq := uint64(i + 1)
		s.ApplyHeartbeat(heartbeat(seq, 1))
		s.ApplyBatch(batch(seq, 0, 1, bot(1, "Alpha", float64(i))))
	}

	if got := len(s.Snapshot().Bots[0].Trail); got != 3 {
		t.Fatalf("trail not capped at 3 points: got %d", got)
	}
}

func TestSnapshotDeepCopiesTrails(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyHeartbeat(heartbeat(1, 1))
	s.ApplyBatch(batch(1, 0, 1, bot(1, "Alpha", 0)))

	snap := s.Snapshot()
	snap.Bots[0].Trail[0].X = 999

	if s.Snapshot().Bots[0].Trail[0].X == 999 {
		t.Fatal("snapshot leaked the store's trail backing array")
	}
}

func TestStatusFreshness(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyHeartbeat(heartbeat(1, 1))
	s.ApplyBatch(batch(1, 0, 1, bot(1, "Alpha", 0)))

	if status := s.Status(); !status.Online || status.Stale {
		t.Fatalf("fresh server reported offline/stale: %+v", status)
	}

	c.Advance(7 * time.Second)
	if status := s.Status(); !status.Online || status.Stale {
		t.Fatalf("server should still be fresh at 7s: %+v", status)
	}

	c.Advance(4 * time.Second)
	status := s.Status()
	if status.Online {
		t.Fatalf("server should be offline at 11s: %+v", status)
	}
	if !status.Stale {
		t.Fatalf("roster should be stale at 11s: %+v", status)
	}
}

func TestServerRestartResetsSessionState(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	// Session A has progressed to a high sequence number.
	s.ApplyHeartbeat(heartbeat(500, 1))
	s.ApplyBatch(batch(500, 0, 1, bot(1, "Alpha", 0)))
	if len(s.Snapshot().Bots) != 1 {
		t.Fatal("session A did not publish")
	}

	// The server restarts: new session, sequence back to 1. Every cycle must
	// still publish instead of being dropped as "old".
	hb := heartbeat(1, 1)
	hb.Session = 42
	if published := s.ApplyHeartbeat(hb); published {
		t.Fatal("non-empty heartbeat published without batches")
	}
	b := batch(1, 0, 1, bot(2, "Beta", 0))
	b.Session = 42
	if published := s.ApplyBatch(b); !published {
		t.Fatal("new session's first cycle was rejected as old")
	}

	snap := s.Snapshot()
	if len(snap.Bots) != 1 || snap.Bots[0].Name != "Beta" {
		t.Fatalf("new session roster wrong: %+v", snap.Bots)
	}
}

func TestSessionChangeClearsOldRoster(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyHeartbeat(heartbeat(1, 1))
	s.ApplyBatch(batch(1, 0, 1, bot(1, "Alpha", 0)))

	// New server process: the old process's bots must not linger while the
	// first cycle of the new session is still being assembled.
	hb := heartbeat(1, 1)
	hb.Session = 43
	s.ApplyHeartbeat(hb)

	if got := len(s.Snapshot().Bots); got != 0 {
		t.Fatalf("old session roster lingered after restart: %d bots", got)
	}
}

func TestConflictingTotalBatchesRestartsCycle(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyHeartbeat(heartbeat(1, 3))
	s.ApplyBatch(batch(1, 0, 3, bot(1, "Alpha", 0)))

	// Conflicting metadata restarts the cycle; this batch is its index 0 and
	// it is the only batch, so the restarted cycle publishes immediately.
	if published := s.ApplyBatch(batch(1, 0, 1, bot(2, "Beta", 0))); !published {
		t.Fatal("restarted single-batch cycle did not publish")
	}
	snap := s.Snapshot()
	if len(snap.Bots) != 1 || snap.Bots[0].Name != "Beta" {
		t.Fatalf("restarted cycle produced wrong roster: %+v", snap.Bots)
	}

	// A batch now out of range for the restarted cycle is rejected.
	if published := s.ApplyBatch(batch(1, 1, 1, bot(3, "Gamma", 0))); published {
		t.Fatal("out-of-range batch published a cycle")
	}
}
