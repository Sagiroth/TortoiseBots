package state

import (
	"sort"
	"strconv"
	"sync"
	"time"

	"tortoise-observability/internal/model"
)

// RaidsStaleAfter is how long the last RAIDS datagram stays valid. The module
// sends one every 2 s snapshot cycle while a raid group stands, so a silence
// of this long means the server stopped, restarted or lost telemetry, and the
// tab falls back to roster-only.
const RaidsStaleAfter = 12 * time.Second

// Bounds on what one datagram may put in the store; a datagram past them is
// trimmed, not rejected. Groups rotate past 40 on the module side.
const (
	maxRaidsGroups = 40
	maxRaidsParts  = 20
)

// raidsState is the latest RAIDS payload. It has its own lock so a 2 s
// datagram never waits on the roster lock.
type raidsState struct {
	mu       sync.Mutex
	session  uint64
	seq      uint64
	ts       int64
	tsMs     int64
	at       time.Time
	list     []model.RaidGroupRow
	partials uint64
	pending  *raidsPending
	// prior holds the sessions this store moved away from, newest last.
	// Numeric order cannot tell a restart from a straggler (a restarted
	// server whose clock is behind reports a lower session), so the
	// rejection of late packets is by identity against this list.
	prior []uint64
}

// raidsPending is a RAIDS cycle that arrived in parts and is not complete
// yet: the parts received so far, keyed by 0-based part number.
type raidsPending struct {
	session uint64
	seq     uint64
	ts      int64
	tsMs    int64
	got     map[int][]model.RaidGroupRow
}

// raidGroupKey names one raid group's copy: one group id can stand on several
// copies, and detail retained for one copy must not leak onto another.
type raidGroupKey struct {
	group, mapID, instance uint32
}

// maxPriorRaidsSessions bounds the superseded sessions ApplyRaids remembers:
// only recent sessions can still have packets in flight, and a hostile
// sender must not grow the store one session id per datagram.
const maxPriorRaidsSessions = 8

// raidsSessionSuperseded reports whether a session id is one this store
// already moved away from: a packet naming it is in flight from before a
// restart, not a restart of its own.
func raidsSessionSuperseded(prior []uint64, session uint64) bool {
	for _, s := range prior {
		if s == session {
			return true
		}
	}
	return false
}

// rememberRaidsSession records a superseded session, newest last, keeping
// the bound above.
func rememberRaidsSession(prior []uint64, session uint64) []uint64 {
	if raidsSessionSuperseded(prior, session) {
		return prior
	}
	if len(prior) >= maxPriorRaidsSessions {
		copy(prior, prior[1:])
		prior = prior[:len(prior)-1]
	}
	return append(prior, session)
}

// ApplyRaids replaces the standing raid rows with the datagram's list. Any
// session change is a restart: rows, sequence and pending parts reset. A late
// packet naming a superseded session is dropped by identity, and a datagram
// older than the one held in the same session is ignored; a cycle split into
// parts takes effect only once all of its parts are in. It reports whether
// the view changed. An empty list clears the rows at once.
func (s *Store) ApplyRaids(p *model.RaidsPayload) bool {
	if p == nil {
		return false
	}
	st := &s.raids
	st.mu.Lock()
	defer st.mu.Unlock()
	if p.Part < 0 || p.Parts < 0 {
		return false
	}
	if p.Parts > 1 && (p.Parts > maxRaidsParts || p.Part >= p.Parts) {
		return false
	}
	if p.Session == st.session {
		if p.TS < st.ts {
			return false
		}
	} else if raidsSessionSuperseded(st.prior, p.Session) {
		// A straggler from before a restart, not a restart of its own.
		return false
	} else {
		// Any session change is a restart, even a lower one: a restarted
		// server whose clock is behind reports a lower session, and the
		// roster treats it as a new session the same way.
		if st.session != 0 {
			st.prior = rememberRaidsSession(st.prior, st.session)
		}
		st.session, st.seq, st.ts = p.Session, 0, 0
		st.pending = nil
	}
	now := s.now()
	if p.Parts <= 1 {
		if p.Seq > 0 && p.Seq <= st.seq {
			return false
		}
		// A whole newer cycle drops an unfinished one: the older set will
		// never complete (the module sends one cycle every 2 s).
		if pend := st.pending; pend != nil && p.Seq > pend.seq {
			st.pending = nil
			st.partials++
		}
		st.applyListLocked(p.Raids, p, now)
		return true
	}
	pend := st.pending
	if pend != nil && p.Seq < pend.seq {
		return false
	}
	if p.Seq <= st.seq && (pend == nil || pend.seq != p.Seq) {
		return false
	}
	if pend != nil && (p.Seq > pend.seq || pend.parts != p.Parts) {
		// A part of a newer cycle (or the same seq with another part count)
		// started: the older set will never complete.
		pend = nil
		st.pending = nil
		st.partials++
	}
	if pend == nil {
		st.pending = &raidsPending{
			session: p.Session, seq: p.Seq, ts: p.TS, tsMs: p.TSMs, parts: p.Parts,
			got: map[int][]model.RaidGroupRow{p.Part: p.Raids},
		}
		return false
	}
	pend.got[p.Part] = p.Raids
	pend.ts, pend.tsMs = p.TS, p.TSMs
	if len(pend.got) < pend.parts {
		return false
	}
	list := make([]model.RaidGroupRow, 0, len(pend.got)*8)
	for i := range pend.parts {
		list = append(list, pend.got[i]...)
	}
	whole := &model.RaidsPayload{Session: pend.session, Seq: pend.seq, TS: pend.ts, TSMs: pend.tsMs}
	st.pending = nil
	st.applyListLocked(list, whole, now)
	return true
}

// applyListLocked replaces the standing rows, capped and normalised. Called
// with the lock held. A row whose payload omits the detail keeps the last
// detail the daemon saw for that group (map + instance + group id): the
// module ships it at most every 5 s on a ~2 s cycle, and the dashboard would
// otherwise lose readiness, cooldown, debuff and mind-control data between
// refreshes. A row that carries detail replaces it; an emptied group (clearing
// the rows) drops it with the row.
func (st *raidsState) applyListLocked(list []model.RaidGroupRow, p *model.RaidsPayload, now time.Time) {
	if len(list) > maxRaidsGroups {
		list = list[:maxRaidsGroups]
	}
	prior := make(map[raidGroupKey]model.RaidGroupRow, len(st.list))
	for _, r := range st.list {
		prior[raidGroupKey{group: r.Group, mapID: r.Map, instance: r.Instance}] = r
	}
	out := make([]model.RaidGroupRow, 0, len(list))
	for _, r := range list {
		if len(r.Detail) == 0 {
			if prev, ok := prior[raidGroupKey{group: r.Group, mapID: r.Map, instance: r.Instance}]; ok && len(prev.Detail) > 0 {
				r.Detail = append([]model.RaidDetailEntry(nil), prev.Detail...)
			}
		}
		out = append(out, r)
	}
	// Own each row's members and keep them ordered: published rows are
	// shared with concurrent views, which may only read them.
	for i := range out {
		m := append([]model.RaidMemberRef(nil), out[i].Members...)
		sort.Slice(m, func(a, b int) bool { return m[a].GUID < m[b].GUID })
		out[i].Members = m
	}
	st.session, st.ts, st.at, st.list = p.Session, p.TS, now, out
	if p.Seq > st.seq {
		st.seq = p.Seq
	}
	st.tsMs = p.TSMs
	if st.tsMs == 0 {
		st.tsMs = p.TS * 1000
	}
}

// RaidsPartials reports how many RAIDS cycles arrived split across parts and
// never completed (their seq was dropped); the listener turns the growth into
// the partial counter.
func (s *Store) RaidsPartials() uint64 {
	st := &s.raids
	st.mu.Lock()
	defer st.mu.Unlock()
	return st.partials
}

// RaidRows returns the fresh RAIDS rows whose fight key is in asked
// (group key "raid#<group>" or the bound commander's name). The live
// snapshot carries them so an open raid card draws positions at once.
// Rows are nil-safe copies: published rows are shared read-only.
func (s *Store) RaidRows(asked map[string]bool) []model.RaidGroupRow {
	st := &s.raids
	st.mu.Lock()
	defer st.mu.Unlock()
	if len(asked) == 0 || s.now().Sub(st.at) > RaidsStaleAfter {
		return nil
	}
	out := make([]model.RaidGroupRow, 0, len(st.list))
	for _, r := range st.list {
		key := r.Key
		if key == "" {
			if r.Commander != "" {
				key = r.Commander
			} else {
				key = raidKey(r.Group)
			}
		}
		if !asked[key] {
			continue
		}
		members := append([]model.RaidMemberRef(nil), r.Members...)
		row := r
		row.Members = members
		out = append(out, row)
	}
	return out
}

// RaidKeysForCommander returns the fight keys of the fresh RAIDS rows
// naming commander (the bound commander's rows): the live fan-out keys of
// the open raid card of a bound group.
func (s *Store) RaidKeysForCommander(commander string) []string {
	if commander == "" {
		return nil
	}
	st := &s.raids
	st.mu.Lock()
	defer st.mu.Unlock()
	if s.now().Sub(st.at) > RaidsStaleAfter {
		return nil
	}
	var out []string
	for _, r := range st.list {
		if r.Commander != commander || r.Key == "" || r.Key == commander {
			continue
		}
		dup := false
		for _, k := range out {
			if k == r.Key {
				dup = true
				break
			}
		}
		if !dup {
			out = append(out, r.Key)
		}
	}
	return out
}

// raidRowByGroup returns the fresh RAIDS row for one group on one instance
// copy, or nil. The join is group + map + instance: one group id can stand on
// several copies at once, and telemetry for one copy must not leak onto
// another.
func raidRowByGroup(rows []model.RaidGroupRow, group, mapID, instance uint32) *model.RaidGroupRow {
	for i := range rows {
		if rows[i].Group == group && rows[i].Map == mapID && rows[i].Instance == instance {
			return &rows[i]
		}
	}
	return nil
}

// Raids builds the view behind GET /api/v1/raids: one run per live raid
// instance copy, groups by roster group id, enriched by the fresh RAIDS rows
// and the bound COMMANDERS bindings.
func (s *Store) Raids() model.RaidsView {
	// Copy the fresh mob entries first: the mobs mutex is never taken while
	// the roster lock is held.
	mobs := s.freshDungeonMobs()

	// The RAIDS rows and the COMMANDERS bindings each have their own lock;
	// copy them before taking the roster lock so the locks never nest.
	s.raids.mu.Lock()
	raidRows := append([]model.RaidGroupRow(nil), s.raids.list...)
	live := !s.raids.at.IsZero() && s.now().Sub(s.raids.at) <= RaidsStaleAfter
	s.raids.mu.Unlock()
	if !live {
		raidRows = nil
	}

	// The bound link needs the standing bindings (a RAIDS row names a
	// commander too, but the run comes from the binding).
	s.commanders.mu.Lock()
	var bindings []model.CommanderBinding
	commanderAt := s.commanders.at
	if !s.commanders.at.IsZero() && s.now().Sub(s.commanders.at) <= CommandersStaleAfter {
		bindings = append(bindings, s.commanders.list...)
		_ = commanderAt
	}
	s.commanders.mu.Unlock()

	s.mu.RLock()
	defer s.mu.RUnlock()

	now := s.now()
	staleModule := false
	runs, _ := s.instanceRunsLocked(s.bots, mobs, now, &staleModule)

	out := make([]model.RaidRun, 0, len(runs))
	for _, r := range runs {
		if !r.raid {
			continue
		}
		// Group the copy's roster members by group id; rows the roster has
		// no group id for (0 = ungrouped) are not a group.
		byGroup := map[uint32][]model.BotSnapshot{}
		var groupIDs []uint32
		var ungrouped []model.BotSnapshot
		for _, m := range groupedMembers(r.run.Key, s.bots) {
			if m.GroupID == 0 {
				ungrouped = append(ungrouped, m)
				continue
			}
			if _, seen := byGroup[m.GroupID]; !seen {
				groupIDs = append(groupIDs, m.GroupID)
			}
			byGroup[m.GroupID] = append(byGroup[m.GroupID], m)
		}
		sort.Slice(groupIDs, func(i, j int) bool { return groupIDs[i] < groupIDs[j] })

		// Attach the fresh RAIDS rows: one for every roster group, plus rows
		// whose group has no roster member on this instance (members all
		// offline or elsewhere show anyway, on the instance the row names).
		seen := map[uint32]bool{}
		groups := make([]model.RaidGroupView, 0, len(groupIDs))
		for _, gid := range groupIDs {
			seen[gid] = true
			row := raidRowByGroup(raidRows, gid, r.run.MapID, r.run.Instance)
			gv := raidGroupView(gid, byGroup[gid], row, bindings, s.bots)
			joinRaidGroupMobs(&gv, r.run, mobs)
			joinRaidGroupExtras(&gv, bindings)
			groups = append(groups, gv)
		}
		for i := range raidRows {
			row := &raidRows[i]
			if row.Map != r.run.MapID || row.Instance != r.run.Instance {
				continue
			}
			if seen[row.Group] {
				continue
			}
			seen[row.Group] = true
			gv := raidGroupView(row.Group, nil, row, bindings, s.bots)
			joinRaidGroupMobs(&gv, r.run, mobs)
			joinRaidGroupExtras(&gv, bindings)
			groups = append(groups, gv)
		}
		sort.Slice(groups, func(i, j int) bool { return groups[i].Group < groups[j].Group })

		var ungroupedMembers []model.DungeonMember
		for _, m := range ungrouped {
			ungroupedMembers = append(ungroupedMembers, dungeonMember(m))
		}
		sortRunMembers(ungroupedMembers)

		rr := model.RaidRun{
			Key: r.run.Key, MapID: r.run.MapID, Instance: r.run.Instance,
			FirstSeen: r.run.FirstSeen, SeenSec: r.run.SeenSec,
			Bots: r.run.Bots, Players: r.run.Players, Dead: r.run.Dead,
			Mobs: r.run.Mobs, MobsMap: r.run.MobsMap, MobsTotal: r.run.MobsTotal,
			Groups:    groups,
			Ungrouped: ungroupedMembers,
		}
		out = append(out, rr)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Key < out[j].Key })

	return model.RaidsView{Runs: out, GeneratedAt: now.Unix(), Live: live, StaleModule: staleModule}
}

// groupedMembers returns the roster members of one run key, in group-id
// order. The roster lock is held.
func groupedMembers(key string, bots map[uint32]*botEntry) []model.BotSnapshot {
	out := []model.BotSnapshot{}
	for _, entry := range bots {
		if dungeonRunKey(entry.snap) == key {
			out = append(out, entry.snap)
		}
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].GroupID != out[j].GroupID {
			return out[i].GroupID < out[j].GroupID
		}
		return out[i].GUID < out[j].GUID
	})
	return out
}

// raidGroupKey is the group's key: the RAIDS key when a fresh row names one,
// else the bound commander's name, else raid#<group>.
func raidGroupKey(group uint32, row *model.RaidGroupRow, bound *model.CommanderBinding) string {
	if row != nil && row.Key != "" {
		return row.Key
	}
	if bound != nil {
		return bound.Commander
	}
	return raidKey(group)
}

// raidKey is the fallback key of an unbound, unobserved group.
func raidKey(group uint32) string {
	return "raid#" + strconv.FormatUint(uint64(group), 10)
}

// boundBindingOf returns the standing COMMANDERS binding for one group.
func boundBindingOf(bindings []model.CommanderBinding, group uint32) *model.CommanderBinding {
	for i := range bindings {
		if bindings[i].Group == group {
			return &bindings[i]
		}
	}
	return nil
}

// raidGroupView joins one group: roster members, the fresh RAIDS row (leader,
// offline members, combat window, detail) and the bound COMMANDERS binding (bound,
// commander, run, layout). A row member outside this run's subset is resolved against
// the full roster first, so a bot standing on another copy keeps its online
// state and fields; only a guid the roster has nowhere, or one the row marks
// offline, is drawn as an offline stub.
func raidGroupView(group uint32, members []model.BotSnapshot, row *model.RaidGroupRow, bindings []model.CommanderBinding, full map[uint32]*botEntry) model.RaidGroupView {
	bound := boundBindingOf(bindings, group)
	g := model.RaidGroupView{Group: group, Key: raidGroupKey(group, row, bound)}
	merged := make([]model.DungeonMember, 0, len(members)+(func() int {
		if row != nil {
			return len(row.Members)
		}
		return 0
	}()))
	byGUID := map[uint32]bool{}
	for _, m := range members {
		merged = append(merged, dungeonMember(m))
		byGUID[m.GUID] = true
	}
	var detailByGUID map[uint32]model.RaidDetailEntry
	if row != nil {
		g.Observed = true
		g.Leader = row.Leader
		g.LeaderGUID = row.LeaderGUID
		g.RaidGroup = row.RaidGroup
		g.More = row.More
		g.Combat = row.Combat
		g.Trimmed = row.Trimmed
		g.Detail = row.Detail
		if len(row.Detail) > 0 {
			detailByGUID = make(map[uint32]model.RaidDetailEntry, len(row.Detail))
			for _, d := range row.Detail {
				detailByGUID[d.GUID] = d
			}
		}
		if row.Commander != "" {
			g.Commander = row.Commander
		}
		for _, ref := range row.Members {
			if byGUID[ref.GUID] {
				continue
			}
			if !ref.Offline {
				if entry, ok := full[ref.GUID]; ok {
					merged = append(merged, dungeonMember(entry.snap))
					continue
				}
			}
			merged = append(merged, model.DungeonMember{
				Name:     ref.Name,
				GUID:     ref.GUID,
				Offline:  true,
				SubGroup: uint32(ref.Sub),
				State:    "offline",
			})
		}
	}
	sortRunMembers(merged)
	g.Members = merged
	if detailByGUID != nil {
		for i := range g.Members {
			if d, ok := detailByGUID[g.Members[i].GUID]; ok {
				g.Members[i].Ready = d.Ready
				g.Members[i].Debuffs = d.Debuffs
				g.Members[i].MC = d.MC
			}
		}
	}
	if bound != nil {
		g.Bound = true
		g.Commander = bound.Commander
		g.Key = raidGroupKey(group, row, bound)
		if bound.Run != nil {
			run := *bound.Run
			g.Run = &run
		}
		if bound.Layout != nil {
			layout := *bound.Layout
			g.Layout = &layout
		}
	} else if row != nil && row.Commander != "" {
		g.Commander = row.Commander
	}
	return g
}

// joinRaidGroupMobs adds the run's DUNGEON_MOBS member ids/targets to the
// group members (the party rows' target column needs the target guid).
func joinRaidGroupMobs(g *model.RaidGroupView, run model.DungeonRun, mobs map[string]dungeonMobsEntry) {
	if g == nil {
		return
	}
	e, ok := mobs[run.Key]
	if !ok {
		return
	}
	byGUID := make(map[uint32]model.DungeonMobsMember, len(e.run.Members))
	for _, m := range e.run.Members {
		byGUID[m.GUID] = m
	}
	for i := range g.Members {
		if m, ok := byGUID[g.Members[i].GUID]; ok {
			g.Members[i].ID = m.ID
			g.Members[i].Target = m.Target
		}
	}
}

// joinRaidGroupExtras copies the bound COMMANDERS binding's per-bot
// activity fields (st/act/cast/cc/rest/mv/tname) onto the group members,
// so the party rows read "doing" without a RAIDS datagram change.
func joinRaidGroupExtras(g *model.RaidGroupView, bindings []model.CommanderBinding) {
	if g == nil || !g.Bound {
		return
	}
	for _, b := range bindings {
		if b.Commander != g.Commander {
			continue
		}
		byGUID := map[uint32]model.CommanderBot{}
		for _, bot := range b.Bots {
			byGUID[bot.GUID] = bot
		}
		for i := range g.Members {
			bot, ok := byGUID[g.Members[i].GUID]
			if !ok {
				continue
			}
			g.Members[i].State2 = bot.State
			g.Members[i].Action = bot.Action
			g.Members[i].Result = bot.Result
			g.Members[i].Cast = bot.Cast
			g.Members[i].CastTarget = bot.CastTarget
			g.Members[i].CC = bot.CC
			g.Members[i].Rest = bot.Rest
			g.Members[i].Moving = bot.Moving
			g.Members[i].TargetName = bot.TargetName
		}
		return
	}
}
