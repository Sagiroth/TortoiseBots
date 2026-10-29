package state

import (
	"sort"
	"sync"
	"time"

	"tortoise-observability/internal/model"
)

// DUNGEON_MOBS (issue #1101): the mobs, member ids and targets of every live
// dungeon run, reported by the module once per snapshot cycle. The state
// lives beside the roster in its own mutex: a 2 s datagram never waits on the
// roster lock, and Dungeons() copies the fresh entries before taking it (no
// nested locks).

const (
	// maxDungeonMobsPerRun and maxDungeonMobsMembers bound what one datagram
	// can make the daemon hold (the module sends at most 48 and 40).
	maxDungeonMobsPerRun  = 64
	maxDungeonMobsMembers = 40
	// maxDungeonMobsRuns bounds the whole map. The module sends at most 32
	// runs per cycle and rotates the rest (up to 96 live runs behind three
	// cycles); keep headroom above that so a rotation is never evicted.
	maxDungeonMobsRuns = 128
	// DungeonMobsStaleAfter is how long a run's mob entry stays valid: four
	// snapshot cycles. With the rotation above, a run may be refreshed only
	// every third cycle (~6 s), so four cycles leave one cycle of slack. A
	// run that leaves the instance stops being refreshed and its mobs vanish
	// a few seconds later; the module's "runs":[] marker clears the rest at
	// once.
	DungeonMobsStaleAfter = 8 * time.Second
)

// dungeonMobsEntry is one run's newest payload plus when it arrived.
type dungeonMobsEntry struct {
	seq uint64
	at  time.Time
	run model.DungeonMobsRun
}

// dungeonMobsState is the DUNGEON_MOBS side of the store. Keyed by the same
// run key as the roster (DungeonKey), so the join needs no matching pass.
type dungeonMobsState struct {
	mu      sync.Mutex
	session uint64
	// seq is the newest datagram sequence applied in this session: a part
	// older than it is a reordered datagram and ignored; parts of one cycle
	// share the seq and are all applied.
	seq  uint64
	runs map[string]dungeonMobsEntry
}

// ApplyDungeonMobs folds one DUNGEON_MOBS datagram into the store. Any
// session change starts from an empty map (the roster does the same: a new
// game-server process is a new session even if its clock is behind), and a
// datagram whose seq is older than the newest applied one is dropped (UDP
// reorder). An empty "runs" list with the newest seq ends every run.
func (s *Store) ApplyDungeonMobs(p *model.DungeonMobsPayload) {
	if p == nil {
		return
	}
	st := &s.dungeonMobs
	st.mu.Lock()
	defer st.mu.Unlock()

	if p.Session != st.session {
		st.session = p.Session
		st.seq = 0
		st.runs = make(map[string]dungeonMobsEntry)
	}
	if p.Seq < st.seq {
		return // a reordered datagram from an older cycle
	}
	if st.runs == nil {
		st.runs = make(map[string]dungeonMobsEntry)
	}

	now := s.now()
	if len(p.Runs) == 0 {
		// The module's end marker: the last run is gone. Only the newest seq
		// may clear, so a late part of an earlier cycle cannot wipe the map.
		st.seq = p.Seq
		st.runs = make(map[string]dungeonMobsEntry)
		return
	}
	st.seq = p.Seq

	for _, r := range p.Runs {
		key := DungeonKey(r.Map, r.Instance)
		st.runs[key] = dungeonMobsEntry{seq: p.Seq, at: now, run: trimDungeonMobsRun(r, p.TS)}
	}
	st.pruneLocked(now)
}

// trimDungeonMobsRun caps one run to what the daemon keeps. The module already
// sends less; this is the belt to its braces (a hostile or buggy sender must
// not grow the store). A boss's cast also gets the moment it ends (ts + the
// time left the module reported), as the commanders view notes it.
func trimDungeonMobsRun(r model.DungeonMobsRun, ts int64) model.DungeonMobsRun {
	if len(r.Members) > maxDungeonMobsMembers {
		r.Members = r.Members[:maxDungeonMobsMembers]
	}
	if r.Mobs != nil {
		mobs := *r.Mobs
		if len(mobs) > maxDungeonMobsPerRun {
			mobs = mobs[:maxDungeonMobsPerRun]
		}
		casting := false
		for i := range mobs {
			casting = casting || mobs[i].CastLeftMS > 0
		}
		if casting && ts > 0 {
			copied := make([]model.CommanderMob, len(mobs)) // a copy: the payload's own rows are not changed
			copy(copied, mobs)
			for i := range copied {
				if copied[i].CastLeftMS > 0 {
					copied[i].CastEndMS = ts*1000 + int64(copied[i].CastLeftMS)
				}
			}
			mobs = copied
		}
		r.Mobs = &mobs
	}
	return r
}

// pruneLocked drops entries past the stale limit and, only if a sender
// ignored every cap, the oldest runs past maxDungeonMobsRuns. The lock is held.
func (st *dungeonMobsState) pruneLocked(now time.Time) {
	for key, e := range st.runs {
		if now.Sub(e.at) > DungeonMobsStaleAfter {
			delete(st.runs, key)
		}
	}
	if len(st.runs) <= maxDungeonMobsRuns {
		return
	}
	keys := make([]string, 0, len(st.runs))
	for key := range st.runs {
		keys = append(keys, key)
	}
	sort.Slice(keys, func(i, j int) bool {
		a, b := st.runs[keys[i]], st.runs[keys[j]]
		if a.seq != b.seq {
			return a.seq > b.seq
		}
		return keys[i] < keys[j]
	})
	for _, key := range keys[maxDungeonMobsRuns:] {
		delete(st.runs, key)
	}
}

// freshDungeonMobs copies the entries still inside the stale limit. Called
// before Dungeons() takes the roster lock, so the two locks are never nested.
func (s *Store) freshDungeonMobs() map[string]dungeonMobsEntry {
	st := &s.dungeonMobs
	st.mu.Lock()
	defer st.mu.Unlock()

	now := s.now()
	st.pruneLocked(now)
	out := make(map[string]dungeonMobsEntry, len(st.runs))
	for key, e := range st.runs {
		out[key] = e
	}
	return out
}

// matchDungeonMobs pairs fresh DUNGEON_MOBS entries with the roster runs they
// describe, returning run index -> entry.
//
// A run whose roster reports a non-zero instance is matched by (map, instance)
// only. A run whose roster reports no instance (the pre-#895 module base: the
// live game server sends instance ids in DUNGEON_MOBS but not in BOT_BATCH) is
// matched by map AND at least one shared member guid — never by map alone — and
// only when exactly one roster run on that map matches; two candidates are
// ambiguous and nothing is joined. Each datagram run joins at most one roster
// run, and each roster run takes at most one datagram run.
func matchDungeonMobs(runs []model.DungeonRun, entries map[string]dungeonMobsEntry) map[int]dungeonMobsEntry {
	out := make(map[int]dungeonMobsEntry)
	if len(runs) == 0 || len(entries) == 0 {
		return out
	}
	keys := make([]string, 0, len(entries))
	for key := range entries {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	usedRun := make([]bool, len(runs))
	usedEntry := make(map[string]bool, len(entries))

	// The roster knows the copy: its (map, instance) key is authoritative.
	for _, key := range keys {
		e := entries[key]
		for i := range runs {
			if usedRun[i] || runs[i].Instance == 0 || runs[i].MapID != e.run.Map || runs[i].Instance != e.run.Instance {
				continue
			}
			out[i] = e
			usedRun[i] = true
			usedEntry[key] = true
			break
		}
	}

	// The roster reports no instance: map plus a shared member guid, and only
	// an unambiguous match.
	for _, key := range keys {
		if usedEntry[key] {
			continue
		}
		e := entries[key]
		candidates := make([]int, 0, 1)
		for i := range runs {
			if usedRun[i] || runs[i].Instance != 0 || runs[i].MapID != e.run.Map {
				continue
			}
			if sharesDungeonMember(runs[i].Members, e.run.Members) {
				candidates = append(candidates, i)
			}
		}
		if len(candidates) != 1 {
			continue // none, or ambiguous: two roster runs share a member with it
		}
		out[candidates[0]] = e
		usedRun[candidates[0]] = true
		usedEntry[key] = true
	}
	return out
}

// sharesDungeonMember reports whether a roster run and a DUNGEON_MOBS run have
// at least one member guid in common (both are low guids of the same unit).
func sharesDungeonMember(members []model.DungeonMember, mobMembers []model.DungeonMobsMember) bool {
	if len(members) == 0 || len(mobMembers) == 0 {
		return false
	}
	have := make(map[uint32]bool, len(mobMembers))
	for _, m := range mobMembers {
		have[m.GUID] = true
	}
	for _, m := range members {
		if have[m.GUID] {
			return true
		}
	}
	return false
}

// joinDungeonMobs adds a fresh DUNGEON_MOBS entry to one run view: the
// members' ids and targets by guid, and the mob list (a reported-empty list
// stays a non-nil empty slice, so the page knows the layers have data).
func joinDungeonMobs(run *model.DungeonRun, e dungeonMobsEntry) {
	byGUID := make(map[uint32]model.DungeonMobsMember, len(e.run.Members))
	for _, m := range e.run.Members {
		byGUID[m.GUID] = m
	}
	for i := range run.Members {
		if m, ok := byGUID[run.Members[i].GUID]; ok {
			run.Members[i].ID = m.ID
			run.Members[i].Target = m.Target
		}
	}
	if e.run.Mobs == nil {
		return
	}
	mobs := make([]model.CommanderMob, len(*e.run.Mobs))
	copy(mobs, *e.run.Mobs)
	mapID := e.run.MobsMap
	run.Mobs = &mobs
	run.MobsMap = &mapID
	run.MobsTotal = e.run.MobsTotal
}
