package state

import (
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"tortoise-observability/internal/model"
)

// raidBot builds one roster row on a raid map.
func raidBot(guid uint32, name string, player bool, mapID, instance, group uint32) model.BotSnapshot {
	inst := instance
	return model.BotSnapshot{
		GUID: guid, Name: name, Player: player, Kind: "pool",
		MapID: mapID, Instance: &inst, GroupID: group, Raid: true,
		Class: "warrior", Role: "dps", Level: 60,
		HP: 100, MaxHP: 100, State: "idle",
	}
}

func raidsRow(group uint32, key string, leader string, leaderGUID uint32, combat *model.CommanderCombat) model.RaidGroupRow {
	return model.RaidGroupRow{
		Group: group, Key: key, Map: 409, Instance: 7,
		Leader: leader, LeaderGUID: leaderGUID, Combat: combat,
	}
}

func raidsPayload(session, seq uint64, ts int64, rows ...model.RaidGroupRow) *model.RaidsPayload {
	return &model.RaidsPayload{V: 12, Session: session, Seq: seq, TS: ts, Type: "RAIDS", Raids: rows}
}

// Roster rows sharing a group land in one raid group, different groups in two,
// and a row without a group is ungrouped. With no RAIDS datagram the view is
// live:false and the keys fall back to raid#<group>.
func TestRaidsGroupsByRosterGroup(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	bots := []model.BotSnapshot{
		raidBot(1, "Tank", false, 409, 7, 7),
		raidBot(2, "Heal", false, 409, 7, 7),
		raidBot(3, "DPS", false, 409, 7, 7),
		raidBot(4, "Off", false, 409, 7, 9),
		raidBot(5, "Off2", false, 409, 7, 9),
		raidBot(6, "Solo", false, 409, 7, 0),
	}
	if ok := s.ApplyBatch(batch(1, 0, 1, bots...)); !ok {
		t.Fatal("batch not published")
	}
	view := s.Raids()
	if len(view.Runs) != 1 || view.Runs[0].Key != "409:7" {
		t.Fatalf("want one run 409:7, got %+v", view.Runs)
	}
	run := view.Runs[0]
	if len(run.Groups) != 2 {
		t.Fatalf("want groups 7 and 9, got %+v", run.Groups)
	}
	byGroup := map[uint32]model.RaidGroupView{}
	for _, g := range run.Groups {
		byGroup[g.Group] = g
	}
	if len(byGroup[7].Members) != 3 || len(byGroup[9].Members) != 2 {
		t.Fatalf("group members wrong: %+v", run.Groups)
	}
	if len(run.Ungrouped) != 1 || run.Ungrouped[0].Name != "Solo" {
		t.Fatalf("ungrouped wrong: %+v", run.Ungrouped)
	}
	if view.Live {
		t.Fatal("no RAIDS datagram: live must be false")
	}
	if byGroup[7].Key != "raid#7" || byGroup[9].Key != "raid#9" {
		t.Fatalf("keys must fall back to raid#<group>: %+v", run.Groups)
	}
	if byGroup[7].Observed {
		t.Fatal("no RAIDS row: observed must be false")
	}
}

// A fresh RAIDS row adds the leader, an offline member and the combat window;
// a row older than RaidsStaleAfter is ignored.
func TestRaidsJoinRaidsRow(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	bots := []model.BotSnapshot{
		raidBot(1, "Tank", false, 409, 7, 7),
		raidBot(2, "Heal", false, 409, 7, 7),
	}
	if ok := s.ApplyBatch(batch(1, 0, 1, bots...)); !ok {
		t.Fatal("batch not published")
	}
	combat := &model.CommanderCombat{WinS: 10, FightMS: 20000}
	row := raidsRow(7, "raid#7", "Celindra", 3, combat)
	row.Members = []model.RaidMemberRef{{GUID: 3, Name: "Celindra", Sub: 0, Offline: true}}
	if !s.ApplyRaids(raidsPayload(9, 1, c.now.Unix(), row)) {
		t.Fatal("RAIDS payload refused")
	}
	run := s.Raids().Runs[0]
	if len(run.Groups) != 1 {
		t.Fatalf("want one group, got %+v", run.Groups)
	}
	g := run.Groups[0]
	if !g.Observed || g.Leader != "Celindra" || g.LeaderGUID != 3 {
		t.Fatalf("leader not joined: %+v", g)
	}
	if len(g.Members) != 3 {
		t.Fatalf("offline member missing: %+v", g.Members)
	}
	var off *model.DungeonMember
	for i := range g.Members {
		if g.Members[i].Offline {
			off = &g.Members[i]
		}
	}
	if off == nil || off.Name != "Celindra" || off.GUID != 3 {
		t.Fatalf("offline member wrong: %+v", g.Members)
	}
	if g.Combat == nil || g.Combat.WinS != 10 || g.Combat.FightMS != 20000 {
		t.Fatalf("combat window not carried raw: %+v", g.Combat)
	}
	if !s.Raids().Live {
		t.Fatal("fresh RAIDS: live must be true")
	}

	// A stale row is ignored: the view falls back to roster-only.
	c.Advance(RaidsStaleAfter + time.Second)
	view := s.Raids()
	if view.Live {
		t.Fatal("stale RAIDS: live must be false")
	}
	g = view.Runs[0].Groups[0]
	if g.Observed || g.Leader != "" || len(g.Members) != 2 || g.Combat != nil {
		t.Fatalf("stale row must be ignored: %+v", g)
	}
}

// A COMMANDERS binding with the same group links the raid group: bound,
// commander and the run copy, with the key becoming the commander. Works
// with no RAIDS datagram at all.
func TestRaidsBoundLink(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	bots := []model.BotSnapshot{
		raidBot(1, "Tank", false, 409, 7, 7),
		raidBot(2, "Heal", false, 409, 7, 7),
	}
	if ok := s.ApplyBatch(batch(1, 0, 1, bots...)); !ok {
		t.Fatal("batch not published")
	}
	p := commandersPayload(9, c.now.Unix(), "Hypeer")
	p.Commanders[0].Group = 7
	p.Commanders[0].Run = &model.CommanderRun{Plan: "mc", State: "running"}
	p.Commanders[0].Layout = &model.CommanderLayout{MainTank: "Tank"}
	if !s.ApplyCommanders(p) {
		t.Fatal("COMMANDERS payload refused")
	}
	run := s.Raids().Runs[0]
	g := run.Groups[0]
	if !g.Bound || g.Commander != "Hypeer" {
		t.Fatalf("bound link missing: %+v", g)
	}
	if g.Key != "Hypeer" {
		t.Fatalf("bound group key must be the commander, got %q", g.Key)
	}
	if g.Run == nil || g.Run.Plan != "mc" {
		t.Fatalf("binding run not copied: %+v", g.Run)
	}
	if g.Layout == nil || g.Layout.MainTank != "Tank" {
		t.Fatalf("binding layout not copied: %+v", g.Layout)
	}
}

// A cycle split in parts applies only when every part arrived; a newer seq
// drops the unfinished one; an empty list clears the rows at once.
func TestRaidsParts(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	if ok := s.ApplyBatch(batch(1, 0, 1, raidBot(1, "Tank", false, 409, 7, 7))); !ok {
		t.Fatal("batch not published")
	}
	ts := c.now.Unix()
	r7 := raidsRow(7, "raid#7", "Tank", 1, nil)
	mk := func(part, parts int, rows ...model.RaidGroupRow) *model.RaidsPayload {
		p := raidsPayload(9, 4, ts, rows...)
		p.Part, p.Parts = part, parts
		return p
	}
	if s.ApplyRaids(mk(0, 2, r7)) {
		t.Fatal("a partial cycle must not apply")
	}
	if g := s.Raids().Runs[0].Groups[0]; g.Observed {
		t.Fatal("partial cycle visible before it completed")
	}
	// A newer seq drops the unfinished one: group 9 stands, group 7 falls
	// back to roster-only.
	r9 := raidsRow(9, "raid#9", "Off", 2, nil)
	newer := raidsPayload(9, 5, ts, r9)
	if !s.ApplyRaids(newer) {
		t.Fatal("whole newer cycle refused")
	}
	byGroup := map[uint32]model.RaidGroupView{}
	for _, g := range s.Raids().Runs[0].Groups {
		byGroup[g.Group] = g
	}
	if g := byGroup[9]; !g.Observed || g.Leader != "Off" {
		t.Fatalf("newer cycle must replace, got %+v", s.Raids().Runs[0].Groups)
	}
	if g := byGroup[7]; g.Observed {
		t.Fatalf("dropped cycle's row must not survive: %+v", g)
	}
	// An empty list clears the rows at once.
	if !s.ApplyRaids(raidsPayload(9, 6, ts)) {
		t.Fatal("empty RAIDS list refused")
	}
	if g := s.Raids().Runs[0].Groups[0]; g.Observed || g.Leader != "" {
		t.Fatalf("empty list must clear rows: %+v", g)
	}
}

// IsRaidRun: the roster flag wins; without it a fresh DUNGEON_MOBS raid flag
// still marks a raid; neither source means dungeon.
func TestIsRaidRun(t *testing.T) {
	raid := raidBot(1, "Tank", false, 409, 7, 7)
	if !IsRaidRun([]model.BotSnapshot{raid}, false) {
		t.Fatal("roster raid flag must win")
	}
	plain := raid
	plain.Raid = false
	if !IsRaidRun([]model.BotSnapshot{plain}, true) {
		t.Fatal("fresh DUNGEON_MOBS raid flag must mark a raid")
	}
	if IsRaidRun([]model.BotSnapshot{plain}, false) {
		t.Fatal("neither source: not a raid")
	}
	if IsRaidRun(nil, false) {
		t.Fatal("empty run: not a raid")
	}
}

// Any session change is a restart, even a lower one: a restarted server whose
// clock is behind reports a lower session, and the rows reset instead of the
// packet being refused. A straggler naming the superseded session is dropped
// by identity, not by numeric order.
func TestRaidsLowerSessionRestarts(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	if ok := s.ApplyBatch(batch(1, 0, 1, raidBot(1, "Tank", false, 409, 7, 7))); !ok {
		t.Fatal("batch not published")
	}
	ts := c.now.Unix()
	before := raidsRow(7, "raid#7", "Tank", 1, nil)
	if !s.ApplyRaids(raidsPayload(9, 3, ts, before)) {
		t.Fatal("first session refused")
	}
	// A lower session still replaces the rows: the state reset with it.
	after := raidsRow(7, "raid#7", "Heal", 2, nil)
	if !s.ApplyRaids(raidsPayload(6, 1, ts, after)) {
		t.Fatal("lower-session restart refused")
	}
	if g := s.Raids().Runs[0].Groups[0]; !g.Observed || g.Leader != "Heal" {
		t.Fatalf("lower session must reset state, got %+v", g)
	}
	// A straggler from before the restart names the superseded session.
	if s.ApplyRaids(raidsPayload(9, 4, ts, before)) {
		t.Fatal("late packet from the superseded session applied")
	}
	if g := s.Raids().Runs[0].Groups[0]; !g.Observed || g.Leader != "Heal" {
		t.Fatalf("straggler must not clobber the restart, got %+v", g)
	}
	// Sequence numbering restarts with the session: an earlier seq of the new
	// session still applies.
	next := raidsRow(7, "raid#7", "Off", 3, nil)
	if !s.ApplyRaids(raidsPayload(6, 2, ts, next)) {
		t.Fatal("new session seq refused")
	}
	if g := s.Raids().Runs[0].Groups[0]; g.Leader != "Off" {
		t.Fatalf("new session rows must stand, got %+v", g)
	}
}

// One group id can stand on two instance copies at once: the RAIDS telemetry
// for one copy must not leak onto the other.
func TestRaidsSplitGroupByInstance(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	bots := []model.BotSnapshot{
		raidBot(1, "TankA", false, 409, 7, 7),
		raidBot(2, "TankB", false, 409, 8, 7),
	}
	if ok := s.ApplyBatch(batch(1, 0, 1, bots...)); !ok {
		t.Fatal("batch not published")
	}
	ts := c.now.Unix()
	combat := &model.CommanderCombat{WinS: 10, FightMS: 20000}
	row := raidsRow(7, "raid#7", "TankA", 1, combat)
	row.Members = []model.RaidMemberRef{{GUID: 9, Name: "Away", Sub: 0, Offline: true}}
	if !s.ApplyRaids(raidsPayload(9, 1, ts, row)) {
		t.Fatal("RAIDS payload refused")
	}
	view := s.Raids()
	if len(view.Runs) != 2 {
		t.Fatalf("want one run per copy, got %+v", view.Runs)
	}
	byRun := map[string]model.RaidRun{}
	for _, r := range view.Runs {
		byRun[r.Key] = r
	}
	got := byRun["409:7"].Groups[0]
	if !got.Observed || got.Leader != "TankA" || got.Combat == nil {
		t.Fatalf("copy with telemetry must join it: %+v", got)
	}
	var names []string
	for _, m := range got.Members {
		names = append(names, m.Name)
	}
	if len(names) != 2 || names[0] != "Away" || names[1] != "TankA" {
		t.Fatalf("copy members wrong: %+v", got.Members)
	}
	other := byRun["409:8"].Groups[0]
	if other.Observed || other.Leader != "" || other.Combat != nil {
		t.Fatalf("other copy must stay roster-only: %+v", other)
	}
	if len(other.Members) != 1 || other.Members[0].Name != "TankB" {
		t.Fatalf("other copy members wrong: %+v", other.Members)
	}
}

// A RAIDS row member that is not on this run's copy is resolved against the
// full roster first: an online bot elsewhere keeps its state and fields, and
// only a guid the roster has nowhere (or the row marks offline) is an offline
// stub.
func TestRaidsRowMemberResolvedAgainstFullRoster(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	bots := []model.BotSnapshot{
		raidBot(1, "Tank", false, 409, 7, 7),
		raidBot(2, "Roamer", false, 409, 8, 7),
	}
	if ok := s.ApplyBatch(batch(1, 0, 1, bots...)); !ok {
		t.Fatal("batch not published")
	}
	ts := c.now.Unix()
	row := raidsRow(7, "raid#7", "Tank", 1, nil)
	row.Members = []model.RaidMemberRef{
		{GUID: 2, Name: "Roamer", Sub: 1},
		{GUID: 3, Name: "Gone", Sub: 0, Offline: true},
		{GUID: 4, Name: "Ghost", Sub: 0},
	}
	if !s.ApplyRaids(raidsPayload(9, 1, ts, row)) {
		t.Fatal("RAIDS payload refused")
	}
	byRun := map[string]model.RaidRun{}
	for _, r := range s.Raids().Runs {
		byRun[r.Key] = r
	}
	g := byRun["409:7"].Groups[0]
	byGUID := map[uint32]model.DungeonMember{}
	for _, m := range g.Members {
		byGUID[m.GUID] = m
	}
	if m := byGUID[2]; m.Offline || m.Name != "Roamer" || m.State != "idle" {
		t.Fatalf("online bot elsewhere must stay online: %+v", byGUID)
	}
	if m := byGUID[3]; !m.Offline || m.State != "offline" {
		t.Fatalf("row-marked offline must be a stub: %+v", byGUID)
	}
	if m := byGUID[4]; !m.Offline || m.Name != "Ghost" {
		t.Fatalf("guid nowhere in the roster must be a stub: %+v", byGUID)
	}
	if m := byGUID[2]; m.SubGroup != 0 {
		t.Fatalf("roster subgroup wins for the online bot: %+v", m)
	}
}

// Concurrent Raids() views must not race the listener's ApplyRaids updates:
// the stored rows are shared read-only once published.
func TestRaidsConcurrentViewAndUpdate(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	bots := []model.BotSnapshot{
		raidBot(1, "Tank", false, 409, 7, 7),
		raidBot(2, "Heal", false, 409, 7, 7),
	}
	if ok := s.ApplyBatch(batch(1, 0, 1, bots...)); !ok {
		t.Fatal("batch not published")
	}
	row := raidsRow(7, "raid#7", "Celindra", 3, nil)
	row.Members = []model.RaidMemberRef{
		{GUID: 5, Name: "Five", Sub: 0, Offline: true},
		{GUID: 3, Name: "Celindra", Sub: 0, Offline: true},
		{GUID: 4, Name: "Four", Sub: 0, Offline: true},
	}
	if !s.ApplyRaids(raidsPayload(9, 1, c.now.Unix(), row)) {
		t.Fatal("RAIDS payload refused")
	}
	var wg sync.WaitGroup
	var seq atomic.Uint64
	seq.Store(2)
	for range 8 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			ts := c.now.Unix()
			for range 25 {
				s.ApplyRaids(raidsPayload(9, seq.Add(1), ts, row))
				view := s.Raids()
				for _, run := range view.Runs {
					for _, g := range run.Groups {
						for _, m := range g.Members {
							_ = m.GUID
						}
					}
				}
			}
		}()
	}
	wg.Wait()
	for i, ref := range row.Members {
		want := []uint32{5, 3, 4}[i]
		if ref.GUID != want {
			t.Fatalf("writer row must stay untouched, got %+v", row.Members)
		}
	}
	g := s.Raids().Runs[0].Groups[0]
	seen := map[uint32]bool{}
	for _, m := range g.Members {
		if m.Offline {
			seen[m.GUID] = true
		}
	}
	if len(seen) != 3 || !seen[3] || !seen[4] || !seen[5] {
		t.Fatalf("stored offline members wrong: %+v", g.Members)
	}
}

// The G2 detail rides the row into the members (rdy/db/mc), and the live
// snapshot serves rows by fight key.
func TestRaidsDetailJoin(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	bots := []model.BotSnapshot{
		raidBot(1, "Tank", false, 409, 7, 7),
		raidBot(2, "Heal", false, 409, 7, 7),
	}
	if ok := s.ApplyBatch(batch(1, 0, 1, bots...)); !ok {
		t.Fatal("batch not published")
	}
	ready := &model.CommanderReady{B: 3, C: 5, CR: 2, CN: 3}
	debuff := []model.CommanderAura{{ID: 100, Name: "Burn"}}
	row := raidsRow(7, "raid#7", "Celindra", 3, nil)
	row.Detail = []model.RaidDetailEntry{
		{GUID: 1, Ready: ready},
		{GUID: 2, Debuffs: debuff, MC: 1},
	}
	if !s.ApplyRaids(raidsPayload(9, 1, c.now.Unix(), row)) {
		t.Fatal("RAIDS payload refused")
	}
	g := s.Raids().Runs[0].Groups[0]
	byGUID := map[uint32]model.DungeonMember{}
	for _, m := range g.Members {
		byGUID[m.GUID] = m
	}
	if byGUID[1].Ready == nil || byGUID[1].Ready.B != 3 {
		t.Fatalf("ready facts not joined: %+v", byGUID[1])
	}
	if len(byGUID[2].Debuffs) != 1 || byGUID[2].Debuffs[0].Name != "Burn" || byGUID[2].MC != 1 {
		t.Fatalf("debuffs/mc not joined: %+v", byGUID[2])
	}
	if len(g.Detail) != 2 {
		t.Fatalf("group detail not preserved: %+v", g.Detail)
	}
	got := s.RaidRows(map[string]bool{"raid#7": true})
	if len(got) != 1 || got[0].Key != "raid#7" {
		t.Fatalf("RaidRows must serve the asked fight key: %+v", got)
	}
	if got := s.RaidRows(map[string]bool{"raid#8": true}); len(got) != 0 {
		t.Fatalf("RaidRows must not serve an unasked key: %+v", got)
	}
	if got := s.RaidKeysForCommander("Nobody"); len(got) != 0 {
		t.Fatalf("no commander rows: %+v", got)
	}
}

// A detail-free cycle keeps the last detail until a refresh replaces it: the
// module ships the G2 detail at most every 5 s on a ~2 s cycle, so most rows
// omit it. A refreshed detail then replaces the retained one.
func TestRaidsDetailRetainedBetweenRefreshes(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	bots := []model.BotSnapshot{
		raidBot(1, "Tank", false, 409, 7, 7),
		raidBot(2, "Heal", false, 409, 7, 7),
	}
	if ok := s.ApplyBatch(batch(1, 0, 1, bots...)); !ok {
		t.Fatal("batch not published")
	}
	ready := &model.CommanderReady{B: 3, C: 5, CR: 2, CN: 3}
	detailed := raidsRow(7, "raid#7", "Celindra", 3, nil)
	detailed.Detail = []model.RaidDetailEntry{
		{GUID: 1, Ready: ready},
		{GUID: 2, Debuffs: []model.CommanderAura{{ID: 100, Name: "Burn"}}, MC: 1},
	}
	if !s.ApplyRaids(raidsPayload(9, 1, c.now.Unix(), detailed)) {
		t.Fatal("RAIDS payload refused")
	}
	// The cycles between two 5 s refreshes omit the detail; the members and
	// the group row keep the last read.
	plain := raidsRow(7, "raid#7", "Celindra", 3, nil)
	if !s.ApplyRaids(raidsPayload(9, 2, c.now.Unix(), plain)) {
		t.Fatal("detail-free RAIDS payload refused")
	}
	g := s.Raids().Runs[0].Groups[0]
	byGUID := map[uint32]model.DungeonMember{}
	for _, m := range g.Members {
		byGUID[m.GUID] = m
	}
	if byGUID[1].Ready == nil || byGUID[1].Ready.B != 3 {
		t.Fatalf("ready facts lost on a detail-free cycle: %+v", byGUID[1])
	}
	if len(byGUID[2].Debuffs) != 1 || byGUID[2].MC != 1 {
		t.Fatalf("debuffs/mc lost on a detail-free cycle: %+v", byGUID[2])
	}
	if len(g.Detail) != 2 {
		t.Fatalf("group detail lost on a detail-free cycle: %+v", g.Detail)
	}
	// A refresh carrying new detail replaces the retained one.
	refreshed := raidsRow(7, "raid#7", "Celindra", 3, nil)
	refreshed.Detail = []model.RaidDetailEntry{
		{GUID: 2, Debuffs: []model.CommanderAura{{ID: 200, Name: "Shadow"}}},
	}
	if !s.ApplyRaids(raidsPayload(9, 3, c.now.Unix(), refreshed)) {
		t.Fatal("refreshed RAIDS payload refused")
	}
	g = s.Raids().Runs[0].Groups[0]
	byGUID = map[uint32]model.DungeonMember{}
	for _, m := range g.Members {
		byGUID[m.GUID] = m
	}
	if byGUID[1].Ready != nil {
		t.Fatalf("stale ready facts survive a refresh: %+v", byGUID[1])
	}
	if len(byGUID[2].Debuffs) != 1 || byGUID[2].Debuffs[0].Name != "Shadow" {
		t.Fatalf("refreshed debuffs not applied: %+v", byGUID[2])
	}
}
