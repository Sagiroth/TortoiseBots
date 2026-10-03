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
	// xpWindow is how far back XP deltas are kept per bot for the XP/hour
	// rate. Level-ups reset the baseline so a ding never counts as a 4000
	// XP burst in one snapshot.
	xpWindow = 30 * time.Minute
	// maxTrackedXP bounds the per-bot XP sample map; evicted with the roster.
	maxTrackedXP = 5000
	// minXpRateWindowSec floors XP/hour windows so one fast kill cannot
	// extrapolate to 100k XP/h.
	minXpRateWindowSec = 60
)

// Config tunes retention. Zero values fall back to the defaults.
type Config struct {
	TrailPoints int
	BotTTL      time.Duration // staleness threshold for the roster
	RosterTTL   time.Duration // wipe the roster after this without a snapshot
	PendingTTL  time.Duration // abandon incomplete cycles after this
	IssueMinAge time.Duration // only surface issues that persist this long
}

type botEntry struct {
	snap  model.BotSnapshot
	trail []model.Coordinate
}

// xpSample is one published per-bot XP observation.
type xpSample struct {
	at    time.Time
	level uint32
	xp    uint32
}

// xpTrack holds the sliding XP observations plus the last positive-gain time
// behind one bot's XpPerHour / XpGainAgeSec fields.
type xpTrack struct {
	samples []xpSample
	lastGain time.Time
	hasGain  bool
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
	// sessionSince is when the current game-server session was first seen:
	// the window every activity counter covers, surfaced in the activity API
	// so the dashboard can label counters instead of implying lifetime totals.
	sessionSince       time.Time
	pending            map[uint64]*pendingCycle
	lastSeq            uint64
	lastSnapshotAt     time.Time
	snapshotsPublished uint64

	heartbeat   *model.HeartbeatPayload
	heartbeatAt time.Time

	anomalies *ringbuf.RingBuffer
	issues    *issueTracker

	// anomSince, anomByType, anomByAction are the cumulative per-session
	// anomaly counts behind GET /api/v1/anomalies/totals. The incident
	// ring buffer is a rolling 1000-row window (~24 min at 500-bot death
	// rates), so whole-run counts live here instead. They reset with the
	// session (or a roster wipe), exactly like the activity counters;
	// clearing the incident feed does not reset them.
	anomSince    time.Time
	anomByType   map[string]uint64
	anomByAction map[string]map[string]uint64

	// xpTracks holds per-bot XP observations for XP/hour derivation. Keyed
	// by GUID like the roster; pruned on publish and session reset.
	xpTracks map[uint32]*xpTrack
	// serverInfo is the latest SERVER_INFO payload (effective settings).
	// Stored verbatim; cleared on session change only.
	serverInfo *model.ServerInfoPayload

	// Activity rollup fed by the BOT_EVENTS datagrams: per-bot counters plus
	// the pool-wide loot/quest/level feeds. Cleared with the roster.
	activity  map[uint32]*botActivityState
	lootFeed  []model.LootFeedItem
	questFeed []model.QuestFeedItem
	levelFeed []model.ActivityLevelItem

	// gear is the daemon's periodic equipped-gear sweep, keyed by GUID.
	// Replaced wholesale by SetGear on every sweep (it is DB truth, not
	// session state), so it stays bounded by the pool that sweep saw.
	gear map[uint32]model.BotGear
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
	if cfg.IssueMinAge <= 0 {
		cfg.IssueMinAge = DefaultIssueMinAge
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
		issues:    newIssueTracker(cfg.IssueMinAge),
		anomByType:   make(map[string]uint64),
		anomByAction: make(map[string]map[string]uint64),
		xpTracks:  make(map[uint32]*xpTrack),
		activity:  make(map[uint32]*botActivityState),
		gear:      make(map[uint32]model.BotGear),
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

// AddAnomaly stores an anomaly and returns the persisted record. Sustained
// problems also open/refresh an issue episode. STUCK is counter-only: the
// tracker never opens an episode for it, and it is not buffered as an
// incident row — the 60 s STUCK issue episode is the surfaced signal.
func (s *Store) AddAnomaly(a model.AnomalyPayload) model.AnomalyPayload {
	if a.Type == "STUCK" {
		s.mu.Lock()
		now := s.now()
		s.countAnomalyLocked(&a, now)
		s.mu.Unlock()
		s.issues.TouchAnomaly(a, now)
		a.ReceivedAt = now
		return a
	}
	saved := s.anomalies.Add(a)

	s.mu.Lock()
	now := s.now()
	s.countAnomalyLocked(&saved, now)
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
		s.xpTracks = make(map[uint32]*xpTrack)
		s.resetActivityLocked()
		s.resetAnomalyTotalsLocked()
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
		snap.Activity = s.activityForLocked(snap.GUID)
		snap.Gear = s.gearForLocked(snap.GUID)
		bots = append(bots, snap)
	}
	sort.Slice(bots, func(i, j int) bool { return bots[i].Name < bots[j].Name })

	out := model.SnapshotPayload{
		Seq:      s.lastSeq,
		Server:   s.statusLocked(now),
		Bots:     bots,
		Issues:   s.issues.Snapshot(),
		Grinding: s.grindingLocked(bots, now),
	}
	if s.serverInfo != nil {
		cp := *s.serverInfo
		out.Info = &cp
	}
	return out
}

// ApplyServerInfo stores the latest SERVER_INFO payload verbatim. Stale
// sessions are rejected so a delayed datagram from a previous server process
// cannot overwrite the current one.
func (s *Store) ApplyServerInfo(info *model.ServerInfoPayload) {
	if info == nil {
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.session != 0 && info.Session != 0 && info.Session != s.session {
		return
	}
	// Adopt the session when none is established yet (SERVER_INFO won the
	// race against the first heartbeat); otherwise a same-process heartbeat
	// would wipe freshly arrived info via beginSessionLocked.
	if s.session == 0 && info.Session != 0 {
		s.session = info.Session
		s.sessionSince = s.now()
	}
	cp := *info
	s.serverInfo = &cp
}

// ServerInfo returns the latest effective-settings payload, or nil.
func (s *Store) ServerInfo() *model.ServerInfoPayload {
	s.mu.RLock()
	defer s.mu.RUnlock()
	if s.serverInfo == nil {
		return nil
	}
	cp := *s.serverInfo
	return &cp
}

// SetGear replaces the equipped-gear sweep keyed by bot GUID. Called by the
// daemon's slow DB loop (minutes, not ticks); the map it stores is the query's
// whole current answer, so bots that left the pool drop out with it.
func (s *Store) SetGear(stats map[uint32]model.BotGear) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.gear = stats
}

// gearForLocked returns a copy of one bot's gear summary, or nil when the last
// sweep did not cover it (the UI shows "–" rather than a fake 0).
func (s *Store) gearForLocked(guid uint32) *model.BotGear {
	g, ok := s.gear[guid]
	if !ok {
		return nil
	}
	return &g
}

// observeXPLocked folds one published roster into the per-bot XP tracks and
// stamps XpPerHour / XpGainAgeSec onto the stored snaps. Level-ups reset the
// baseline to the ding remainder (never a burst); bots at max level or
// without wire XP keep a zero rate. Samples older than the window are
// pruned, so a flat bot's rate decays to 0.
func (s *Store) observeXPLocked(bots []model.BotSnapshot, now time.Time) {
	if s.xpTracks == nil {
		s.xpTracks = make(map[uint32]*xpTrack)
	}
	seen := make(map[uint32]bool, len(bots))
	for i := range bots {
		b := &bots[i]
		if b.GUID == 0 {
			continue
		}
		seen[b.GUID] = true
		tr := s.xpTracks[b.GUID]
		if tr == nil {
			tr = &xpTrack{}
			s.xpTracks[b.GUID] = tr
		}
		// Prune samples outside the rate window.
		kept := tr.samples[:0]
		for _, sm := range tr.samples {
			if now.Sub(sm.at) <= xpWindow {
				kept = append(kept, sm)
			}
		}
		tr.samples = kept
		prev := xpSample{}
		hasPrev := len(tr.samples) > 0
		if hasPrev {
			prev = tr.samples[len(tr.samples)-1]
		}
		if b.NextXP == 0 || b.XP > b.NextXP {
			// No wire data (old emitter) or corrupt sample: no rate.
			b.XpPerHour = 0
		} else if !hasPrev {
			tr.samples = append(tr.samples, xpSample{at: now, level: b.Level, xp: b.XP})
			b.XpPerHour = 0
		} else if b.Level != prev.level {
			// Ding: keep the pre-ding baseline so the window survives;
			// the ding remainder is the new head, never a burst.
			tr.samples = append(tr.samples, xpSample{at: now, level: b.Level, xp: b.XP})
			// A ding is itself proof of gain.
			tr.lastGain = now
			tr.hasGain = true
			b.XpPerHour = xpRateLocked(tr.samples, now)
		} else if b.XP > prev.xp {
			tr.samples = append(tr.samples, xpSample{at: now, level: b.Level, xp: b.XP})
			tr.lastGain = now
			tr.hasGain = true
			b.XpPerHour = xpRateLocked(tr.samples, now)
		} else {
			// No forward progress: keep the window, report the windowed rate
			// (decays to 0 as old samples age out).
			b.XpPerHour = xpRateLocked(tr.samples, now)
		}
		if tr.hasGain {
			b.XpGainAgeSec = now.Sub(tr.lastGain).Seconds()
		} else {
			b.XpGainAgeSec = -1
		}
	}
	// Drop tracks for departed bots; bound the map against roster churn.
	for guid := range s.xpTracks {
		if !seen[guid] {
			delete(s.xpTracks, guid)
		}
	}
	for len(s.xpTracks) > maxTrackedXP {
		for guid := range s.xpTracks {
			delete(s.xpTracks, guid)
			break
		}
	}
}

// xpRateLocked computes XP/hour across the in-window samples. Cross-level
// spans credit the pre-ding progress plus the ding remainder (levels gained
// x typical level cost is unknowable here, so only same-window XP counts).
// Windows shorter than minXpRateWindowSec return 0 so a single fast kill
// cannot extrapolate to 100k XP/h.
func xpRateLocked(samples []xpSample, now time.Time) float64 {
	if len(samples) < 2 {
		return 0
	}
	base := samples[0]
	last := samples[len(samples)-1]
	secs := now.Sub(base.at).Seconds()
	if secs < minXpRateWindowSec {
		return 0
	}
	if last.level == base.level {
		if last.xp <= base.xp {
			return 0
		}
		return float64(last.xp-base.xp) * 3600 / secs
	}
	// Ding inside the window: progress to finish the old level + remainder.
	if last.level <= base.level || last.xp > 1000000 {
		return 0
	}
	gain := 0
	prevLvl, prevXP := base.level, base.xp
	for _, sm := range samples[1:] {
		if sm.level == prevLvl && sm.xp > prevXP {
			gain += int(sm.xp) - int(prevXP)
		}
		prevLvl, prevXP = sm.level, sm.xp
	}
	if gain <= 0 {
		return 0
	}
	return float64(gain) * 3600 / secs
}

// grindingLocked builds the pool-wide "are they grinding" rollup. Kills come
// from BOT_DEATH anomalies in the ring buffer (no new emitter counter), XP
// from the derived per-bot rates, combat/travel from the roster itself.
func (s *Store) grindingLocked(bots []model.BotSnapshot, now time.Time) model.GrindingSummary {
	out := model.GrindingSummary{}
	n := len(bots)
	if n == 0 {
		return out
	}
	out.BotsTracked = n
	out.StateCounts = map[string]int{}
	rates := make([]float64, 0, n)
	for _, b := range bots {
		rates = append(rates, b.XpPerHour)
		if b.XpGainAgeSec >= 0 && now.Sub(s.lastSnapshotAt) <= 10*time.Minute {
			// Counted as gaining when the last positive delta is recent.
			// 10 min matches the "died in last 5 min" scale below.
			if b.XpGainAgeSec <= 600 {
				out.BotsGainingXP++
			}
		}
		if b.XpPerHour > 0 {
			out.TotalXpHour += b.XpPerHour
		}
		out.StateCounts[b.State]++
	}
	sort.Float64s(rates)
	if len(rates) > 0 {
		out.MedianXpHour = rates[len(rates)/2]
	}
	out.PctGainingXP = float64(out.BotsGainingXP) / float64(n) * 100
	out.LevelBands = levelBands(bots, s.gear)
	// BOT_DEATH anomalies are bot deaths, not bot kills: last-5-min
	// distinct dead bots + per-min casualty rate.
	if s.anomalies != nil {
		recent := s.anomalies.GetRecent(1000, "BOT_DEATH", "")
		cutoff := now.Add(-5 * time.Minute)
		seen := map[uint32]bool{}
		for _, a := range recent {
			if a.ReceivedAt.IsZero() || a.ReceivedAt.After(cutoff) {
				seen[a.GUID] = true
			}
		}
		out.PctDied5Min = float64(len(seen)) / float64(n) * 100
		// Rate over the last 10 min window (or less when young).
		winStart := now.Add(-10 * time.Minute)
		count := 0
		for _, a := range recent {
			if a.ReceivedAt.IsZero() || a.ReceivedAt.After(winStart) {
				count++
			}
		}
		out.DeathsPerMin = float64(count) / 10
	}
	return out
}

// levelBands builds adaptive buckets from the roster itself: per level while
// the pool spans <= 10 levels (launch: L1..L7 visible), otherwise fixed
// five-level buckets 1-5, 6-10, ... 56-60. A single "1-9: 500" row can never
// hide progress again. Each band also carries the mean equipped item level of
// its bots (from the periodic gear sweep), so the pool panel shows whether gear
// keeps pace with level.
func levelBands(bots []model.BotSnapshot, gear map[uint32]model.BotGear) []model.LevelBand {
	if len(bots) == 0 {
		return nil
	}
	minLvl, maxLvl := bots[0].Level, bots[0].Level
	for _, b := range bots[1:] {
		if b.Level < minLvl {
			minLvl = b.Level
		}
		if b.Level > maxLvl {
			maxLvl = b.Level
		}
	}
	if minLvl < 1 {
		minLvl = 1
	}

	narrow := maxLvl-minLvl <= 10
	var bounds [][2]uint32
	if narrow {
		bounds = make([][2]uint32, 0, maxLvl-minLvl+1)
		for lvl := minLvl; lvl <= maxLvl; lvl++ {
			bounds = append(bounds, [2]uint32{lvl, lvl})
		}
	} else {
		for lo := uint32(1); lo <= 56; lo += 5 {
			bounds = append(bounds, [2]uint32{lo, lo + 4})
		}
	}
	bandOf := func(level uint32) int {
		if narrow {
			return int(level - minLvl)
		}
		if level < 1 {
			return 0
		}
		i := int((level - 1) / 5)
		if i >= len(bounds) {
			i = len(bounds) - 1
		}
		return i
	}

	out := make([]model.LevelBand, 0, len(bounds))
	for _, bd := range bounds {
		out = append(out, model.LevelBand{Lo: bd[0], Hi: bd[1]})
	}
	sums := make([]float64, len(bounds))
	for _, b := range bots {
		i := bandOf(b.Level)
		if i < 0 || i >= len(out) {
			continue
		}
		out[i].Count++
		if g, ok := gear[b.GUID]; ok {
			sums[i] += g.ItemLevel
			out[i].GearBots++
		}
	}
	for i := range out {
		if out[i].GearBots > 0 {
			out[i].AvgItemLevel = sums[i] / float64(out[i].GearBots)
		}
	}
	return out
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
	s.observeXPLocked(bots, now)
	s.observeLevelsLocked(bots, now)
	// Drop activity for bots the roster no longer carries; the map stays
	// bounded by the live population instead of growing with bot churn.
	for guid := range s.activity {
		if _, ok := next[guid]; !ok {
			delete(s.activity, guid)
		}
	}
	// Stamp the derived XP fields onto the stored roster so Snapshot serves
	// them without recomputation.
	for _, b := range bots {
		if e, ok := s.bots[b.GUID]; ok {
			e.snap.XpPerHour = b.XpPerHour
			e.snap.XpGainAgeSec = b.XpGainAgeSec
		}
	}
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
	s.sessionSince = s.now()
	s.lastSeq = 0
	s.pending = make(map[uint64]*pendingCycle)
	s.bots = make(map[uint32]*botEntry)
	s.heartbeat = nil
	s.heartbeatAt = time.Time{}
	s.lastSnapshotAt = time.Time{}
	s.xpTracks = make(map[uint32]*xpTrack)
	s.resetActivityLocked()
	s.resetAnomalyTotalsLocked()
	s.serverInfo = nil
	s.issues.Reset()
	s.issues.NoteSessionChange(s.now())
}

// resetActivityLocked drops every activity counter and feed. Called with the
// roster: a new game-server process, or a roster wipe, invalidates them.
func (s *Store) resetActivityLocked() {
	s.activity = make(map[uint32]*botActivityState)
	s.lootFeed = nil
	s.questFeed = nil
	s.levelFeed = nil
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
