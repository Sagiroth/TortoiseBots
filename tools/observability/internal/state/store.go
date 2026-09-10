// Package state is the daemon's single source of truth for live telemetry.
//
// The game server emits a sequence of snapshot cycles: one HEARTBEAT datagram
// followed by zero or more BOT_BATCH datagrams sharing the same Seq. A cycle is
// only published once every batch index has arrived; until then the previous
// roster remains authoritative. This makes the roster immune to duplicate,
// reordered, or lost datagrams: a partial cycle is discarded when the next one
// begins, and a per-bot TTL evicts anything a cycle failed to refresh.
package state

import (
	"sort"
	"sync"
	"time"

	"tortoise-observability/internal/model"
	"tortoise-observability/internal/ringbuf"
)

const (
	defaultTrailPoints = 10
	// defaultBotTTL is how long a published snapshot is considered current.
	defaultBotTTL = 8 * time.Second
	// defaultRosterTTL is when a roster that stopped receiving complete
	// snapshots is wiped entirely rather than shown as indefinitely stale.
	defaultRosterTTL  = 30 * time.Second
	defaultPendingTTL = 10 * time.Second
	// minTrailMoveSq skips trail points for bots that have not meaningfully
	// moved, so stationary bots do not render as a blob.
	minTrailMoveSq = 0.05 * 0.05
)

// Config tunes retention. Zero values fall back to the defaults.
type Config struct {
	TrailPoints int
	BotTTL      time.Duration // staleness threshold for the roster
	RosterTTL   time.Duration // wipe the roster after this without a snapshot
	PendingTTL  time.Duration // abandon incomplete cycles after this
}

type botEntry struct {
	snap  model.BotSnapshot
	trail []model.Coordinate
}

type pendingCycle struct {
	seq        uint64
	startedAt  time.Time
	total      int
	seen       int
	batches    map[int][]model.BotSnapshot
}

// Store holds live server status and the authoritative bot roster.
type Store struct {
	mu  sync.RWMutex
	cfg Config

	now func() time.Time

	bots map[uint32]*botEntry

	// session identifies the game-server process. A restart resets its seq,
	// so a session change resets all sequence and roster state.
	session            uint64
	pending            map[uint64]*pendingCycle
	lastSeq            uint64
	lastSnapshotAt     time.Time
	snapshotsPublished uint64

	heartbeat   *model.HeartbeatPayload
	heartbeatAt time.Time

	anomalies *ringbuf.RingBuffer
	issues    *issueTracker
}

// New creates an empty store. The ring buffer is owned by the caller and is
// safe to share (it carries its own lock).
func New(cfg Config, anomalies *ringbuf.RingBuffer) *Store {
	if cfg.TrailPoints <= 0 {
		cfg.TrailPoints = defaultTrailPoints
	}
	if cfg.BotTTL <= 0 {
		cfg.BotTTL = defaultBotTTL
	}
	if cfg.RosterTTL <= 0 {
		cfg.RosterTTL = defaultRosterTTL
	}
	if cfg.PendingTTL <= 0 {
		cfg.PendingTTL = defaultPendingTTL
	}
	if anomalies == nil {
		anomalies = ringbuf.New(1000)
	}
	return &Store{
		cfg:       cfg,
		now:       time.Now,
		bots:      make(map[uint32]*botEntry),
		pending:   make(map[uint64]*pendingCycle),
		anomalies: anomalies,
		issues:    newIssueTracker(),
	}
}

// SetClock replaces the time source. Intended for tests only.
func (s *Store) SetClock(now func() time.Time) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.now = now
}

// ApplyHeartbeat records a server pulse and opens/refreshes a cycle. An empty
// roster cycle is published immediately; otherwise the roster is only replaced
// once all of the cycle's batches arrive. It returns true when this heartbeat
// published a roster.
func (s *Store) ApplyHeartbeat(hb *model.HeartbeatPayload) bool {
	if hb == nil {
		return false
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	now := s.now()
	s.beginSessionLocked(hb.Session)
	s.heartbeat = hb
	s.heartbeatAt = now
	s.prunePendingLocked(now)

	if hb.Seq == 0 || hb.Seq <= s.lastSeq {
		return false
	}

	if hb.BotsCount == 0 {
		s.publishLocked(hb.Seq, nil, now)
		return true
	}
	return false
}

// ApplyBatch accepts one chunk of a snapshot cycle. It returns true when this
// batch completed the cycle and the store published a new roster.
func (s *Store) ApplyBatch(b *model.BotBatchPayload) bool {
	if b == nil || b.Seq == 0 || b.TotalBatches <= 0 ||
		b.BatchIndex < 0 || b.BatchIndex >= b.TotalBatches {
		return false
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	now := s.now()
	s.beginSessionLocked(b.Session)
	s.prunePendingLocked(now)

	if b.Seq <= s.lastSeq {
		// Old or duplicate cycle: the roster it would produce is already
		// superseded (or will be by the next complete cycle).
		return false
	}

	cycle := s.pending[b.Seq]
	if cycle != nil && cycle.total != b.TotalBatches {
		// Conflicting metadata for the same Seq. Treat this batch as the
		// start of a fresh cycle rather than publishing a partial roster.
		cycle = nil
	}
	if cycle == nil {
		cycle = &pendingCycle{
			seq:       b.Seq,
			startedAt: now,
			total:     b.TotalBatches,
			batches:   make(map[int][]model.BotSnapshot, b.TotalBatches),
		}
		s.pending[b.Seq] = cycle
	}

	if _, duplicate := cycle.batches[b.BatchIndex]; duplicate {
		return false
	}
	cycle.batches[b.BatchIndex] = b.Bots
	cycle.seen++

	if cycle.seen < cycle.total {
		return false
	}

	bots := make([]model.BotSnapshot, 0, cycle.total*4)
	for i := 0; i < cycle.total; i++ {
		bots = append(bots, cycle.batches[i]...)
	}
	delete(s.pending, b.Seq)
	s.publishLocked(b.Seq, bots, now)
	return true
}

// AddAnomaly stores an anomaly and returns the persisted record. Anomaly types
// that represent sustained problems also open/refresh an issue episode.
func (s *Store) AddAnomaly(a model.AnomalyPayload) model.AnomalyPayload {
	saved := s.anomalies.Add(a)

	s.mu.Lock()
	now := s.now()
	s.mu.Unlock()
	s.issues.TouchAnomaly(saved, now)
	return saved
}

// Issues returns active and recently resolved problem episodes.
func (s *Store) Issues() model.IssueSnapshot {
	return s.issues.Snapshot()
}

// Anomalies exposes the shared ring buffer for REST queries.
func (s *Store) Anomalies() *ringbuf.RingBuffer {
	return s.anomalies
}

// Evict abandons incomplete cycles and wipes a roster that has not been
// refreshed by a complete snapshot within RosterTTL. It returns true when the
// roster changed.
func (s *Store) Evict() bool {
	s.mu.Lock()
	defer s.mu.Unlock()

	now := s.now()
	s.prunePendingLocked(now)

	if len(s.bots) == 0 {
		return false
	}
	if s.lastSnapshotAt.IsZero() || now.Sub(s.lastSnapshotAt) > s.cfg.RosterTTL {
		s.bots = make(map[uint32]*botEntry)
		s.issues.Reset()
		return true
	}
	return false
}

// IsOnline reports whether a heartbeat has arrived within 10 seconds.
func (s *Store) IsOnline() bool {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return !s.heartbeatAt.IsZero() && s.now().Sub(s.heartbeatAt) <= 10*time.Second
}

// Status returns the current server view, including freshness metadata.
func (s *Store) Status() model.ServerStatus {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.statusLocked(s.now())
}

// Snapshot returns a coherent roster copy. Trail slices are deep-copied so
// callers can marshal without holding the lock.
func (s *Store) Snapshot() model.SnapshotPayload {
	s.mu.RLock()
	defer s.mu.RUnlock()

	now := s.now()
	bots := make([]model.BotSnapshot, 0, len(s.bots))
	for _, entry := range s.bots {
		snap := entry.snap
		if entry.trail != nil {
			snap.Trail = make([]model.Coordinate, len(entry.trail))
			copy(snap.Trail, entry.trail)
		}
		bots = append(bots, snap)
	}
	sort.Slice(bots, func(i, j int) bool { return bots[i].Name < bots[j].Name })

	return model.SnapshotPayload{
		Seq:    s.lastSeq,
		Server: s.statusLocked(now),
		Bots:   bots,
		Issues: s.issues.Snapshot(),
	}
}

// publishLocked replaces the roster with the contents of a complete cycle.
// Trail continuity is preserved only for bots that stayed on the same
// map/zone; any transition resets the trail so coordinates never leak across
// zone maps.
func (s *Store) publishLocked(seq uint64, bots []model.BotSnapshot, now time.Time) {
	next := make(map[uint32]*botEntry, len(bots))

	for _, snap := range bots {
		if snap.GUID == 0 {
			continue
		}

		var trail []model.Coordinate
		if prev, ok := s.bots[snap.GUID]; ok &&
			prev.snap.MapID == snap.MapID && prev.snap.ZoneID == snap.ZoneID {
			trail = prev.trail
		}
		trail = appendTrail(trail, snap, s.cfg.TrailPoints)

		next[snap.GUID] = &botEntry{
			snap:  snap,
			trail: trail,
		}
	}

	s.bots = next
	s.lastSeq = seq
	s.lastSnapshotAt = now
	s.snapshotsPublished++
	s.issues.Observe(bots, now)
}

func appendTrail(trail []model.Coordinate, snap model.BotSnapshot, limit int) []model.Coordinate {
	if !snap.Projected {
		return trail
	}

	point := model.Coordinate{X: snap.X, Y: snap.Y, PctX: snap.PctX, PctY: snap.PctY}

	if n := len(trail); n > 0 {
		last := trail[n-1]
		dx, dy := point.X-last.X, point.Y-last.Y
		if dx*dx+dy*dy <= minTrailMoveSq {
			return trail
		}
	}

	trail = append(trail, point)
	if len(trail) > limit {
		trimmed := make([]model.Coordinate, limit)
		copy(trimmed, trail[len(trail)-limit:])
		trail = trimmed
	}
	return trail
}

// beginSessionLocked resets sequence and roster state when a different
// game-server process starts. The daemon outlives server restarts, and the
// emitter's seq restarts at 1; without this, every cycle from the new process
// would be rejected as old.
func (s *Store) beginSessionLocked(session uint64) {
	if session == 0 || session == s.session {
		return
	}

	s.session = session
	s.lastSeq = 0
	s.pending = make(map[uint64]*pendingCycle)
	s.bots = make(map[uint32]*botEntry)
	s.heartbeat = nil
	s.heartbeatAt = time.Time{}
	s.lastSnapshotAt = time.Time{}
	s.issues.Reset()
}

func (s *Store) prunePendingLocked(now time.Time) {
	for seq, cycle := range s.pending {
		if now.Sub(cycle.startedAt) > s.cfg.PendingTTL || seq <= s.lastSeq {
			delete(s.pending, seq)
		}
	}
}

func (s *Store) statusLocked(now time.Time) model.ServerStatus {
	status := model.ServerStatus{
		Online:             false,
		Stale:              true,
		Seq:                s.lastSeq,
		SnapshotsPublished: s.snapshotsPublished,
	}

	if hb := s.heartbeat; hb != nil {
		status.Uptime = hb.Uptime
		status.TickDiffMs = hb.TickDiffMs
		status.WindowSecs = hb.WindowSecs
		status.Humans = hb.HumansCount
		status.Bots = hb.BotsCount
		status.States = hb.States
	}

	if !s.heartbeatAt.IsZero() {
		age := now.Sub(s.heartbeatAt)
		status.LastHeartbeatAgeSec = age.Seconds()
		status.Online = age <= 10*time.Second
	}
	if !s.lastSnapshotAt.IsZero() {
		age := now.Sub(s.lastSnapshotAt)
		status.LastSnapshotAgeSec = age.Seconds()
		status.Stale = age > s.cfg.BotTTL
	} else {
		status.Stale = true
	}
	return status
}
