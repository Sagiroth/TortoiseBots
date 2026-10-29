package state

import (
	"encoding/json"
	"fmt"
	"strings"
	"testing"
	"time"

	"tortoise-observability/internal/model"
)

// mobRun builds one DUNGEON_MOBS run with a reported mob list (empty is a
// reported-empty list, never "not reported").
func mobRun(mapID, instance uint32, members []model.DungeonMobsMember, total int, mobs ...model.CommanderMob) model.DungeonMobsRun {
	if mobs == nil {
		mobs = []model.CommanderMob{}
	}
	list := mobs
	return model.DungeonMobsRun{Map: mapID, Instance: instance, Members: members, MobsMap: mapID, MobsTotal: total, Mobs: &list}
}

func mobPayload(session, seq uint64, runs ...model.DungeonMobsRun) *model.DungeonMobsPayload {
	return &model.DungeonMobsPayload{V: 12, Session: session, Seq: seq, Type: "DUNGEON_MOBS", Runs: runs}
}

func mobEntries(s *Store) map[string]dungeonMobsEntry {
	st := &s.dungeonMobs
	st.mu.Lock()
	defer st.mu.Unlock()
	out := make(map[string]dungeonMobsEntry, len(st.runs))
	for k, v := range st.runs {
		out[k] = v
	}
	return out
}

const (
	mobGUID  = "17379391094613082370"
	botGUID  = "10844"
	tankGUID = "10845"
)

func hostileMob(id, victim string) model.CommanderMob {
	return model.CommanderMob{ID: id, Entry: 4863, Name: "Stonevault Rockchewer", HP: 85, X: 1230.5, Y: 850, Victim: victim, Reaction: "h"}
}

// A datagram from an older cycle (lower seq) is dropped even when it carries a
// run the newer one did not (UDP reorder: stale data must not come back), and
// parts of one cycle (same seq) are all applied.
func TestDungeonMobsSessionAndSeqOrdering(t *testing.T) {
	s := newTestStore(newClock())
	runA := mobRun(409, 13, []model.DungeonMobsMember{{GUID: 1, ID: botGUID}}, 1, hostileMob(mobGUID, botGUID))
	runB := mobRun(409, 14, []model.DungeonMobsMember{{GUID: 2, ID: "99"}}, 0,
		model.CommanderMob{ID: "5", Entry: 6, Name: "Rat", HP: 100, X: 1, Y: 2, Reaction: "n", Kind: "k"})

	s.ApplyDungeonMobs(mobPayload(7, 5, runA))
	if got := mobEntries(s); len(got) != 1 || got["409:13"].run.MobsTotal != 1 {
		t.Fatalf("first datagram: %+v", got)
	}
	// An older seq of the same session is a reordered datagram: dropped whole.
	s.ApplyDungeonMobs(mobPayload(7, 4, runB))
	if got := mobEntries(s); len(got) != 1 || got["409:14"].seq != 0 {
		t.Fatalf("older seq applied: %+v", got)
	}
	// A newer seq of the same cycle (parts share it) is applied, run by run.
	s.ApplyDungeonMobs(mobPayload(7, 5, runB))
	if got := mobEntries(s); len(got) != 2 {
		t.Fatalf("same-seq part not applied: %+v", got)
	}
	// The end marker with the newest seq clears everything...
	s.ApplyDungeonMobs(mobPayload(7, 6))
	if got := mobEntries(s); len(got) != 0 {
		t.Fatalf("end marker did not clear: %+v", got)
	}
	// ...and a late part of the older cycle cannot bring a run back.
	s.ApplyDungeonMobs(mobPayload(7, 5, runA))
	if got := mobEntries(s); len(got) != 0 {
		t.Fatalf("late part after the end marker: %+v", got)
	}
}

// Any session change resets the map, not only a higher one: the roster resets
// on any session change and a game server restarting with a clock behind must
// not freeze the mobs feed on the old session.
func TestDungeonMobsSessionChangeResets(t *testing.T) {
	s := newTestStore(newClock())
	runA := mobRun(409, 13, []model.DungeonMobsMember{{GUID: 1, ID: botGUID}}, 1, hostileMob(mobGUID, botGUID))
	runB := mobRun(409, 14, []model.DungeonMobsMember{{GUID: 2, ID: "99"}}, 0,
		model.CommanderMob{ID: "5", Entry: 6, Name: "Rat", HP: 100, X: 1, Y: 2, Reaction: "n", Kind: "k"})

	s.ApplyDungeonMobs(mobPayload(7, 5, runA))
	// A lower session (a restart whose clock is behind) still replaces the map.
	s.ApplyDungeonMobs(mobPayload(6, 1, runB))
	got := mobEntries(s)
	if len(got) != 1 || got["409:14"].run.MobsTotal != 0 || got["409:13"].seq != 0 {
		t.Fatalf("lower session did not reset: %+v", got)
	}
	// And a higher one replaces it again.
	s.ApplyDungeonMobs(mobPayload(8, 1, runA))
	got = mobEntries(s)
	if len(got) != 1 || got["409:13"].run.MobsTotal != 1 || got["409:14"].seq != 0 {
		t.Fatalf("higher session did not reset: %+v", got)
	}
}

// The store never holds more than the caps, whatever a sender does.
func TestDungeonMobsTrims(t *testing.T) {
	s := newTestStore(newClock())
	members := make([]model.DungeonMobsMember, 0, 45)
	for i := range 45 {
		members = append(members, model.DungeonMobsMember{GUID: uint32(i), ID: "g"})
	}
	mobs := make([]model.CommanderMob, 0, 70)
	for i := range 70 {
		mobs = append(mobs, model.CommanderMob{ID: "m", Entry: uint32(i), Name: "Mob", HP: 100, X: float64(i), Y: 0, Reaction: "h"})
	}
	runs := make([]model.DungeonMobsRun, 0, maxDungeonMobsRuns+2)
	for i := range maxDungeonMobsRuns + 2 {
		runs = append(runs, mobRun(uint32(500+i), 1, members, 70, mobs...))
	}
	s.ApplyDungeonMobs(mobPayload(1, 1, runs...))

	got := mobEntries(s)
	if len(got) != maxDungeonMobsRuns {
		t.Fatalf("runs: %d, want %d", len(got), maxDungeonMobsRuns)
	}
	e := got["500:1"]
	if e.run.Mobs == nil || len(*e.run.Mobs) != maxDungeonMobsPerRun {
		t.Fatalf("mobs: %d, want %d", len(*e.run.Mobs), maxDungeonMobsPerRun)
	}
	if len(e.run.Members) != maxDungeonMobsMembers {
		t.Fatalf("members: %d, want %d", len(e.run.Members), maxDungeonMobsMembers)
	}
}

// A run that stops being reported survives the module's rotation gap and then
// vanishes; the join drops it without touching the roster run. The times are
// fixed (7 s still fresh, 9 s stale) so changing DungeonMobsStaleAfter fails
// this test.
func TestDungeonMobsStaleExpiry(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	if ok := s.ApplyBatch(batch(1, 0, 1, du(1, "Tank", false, 409, 13))); !ok {
		t.Fatal("batch not published")
	}
	s.ApplyDungeonMobs(mobPayload(7, 5, mobRun(409, 13, []model.DungeonMobsMember{{GUID: 1, ID: botGUID}}, 1, hostileMob(mobGUID, botGUID))))

	view := s.Dungeons()
	if len(view.Runs) != 1 || view.Runs[0].Mobs == nil || len(*view.Runs[0].Mobs) != 1 || view.Runs[0].MobsTotal != 1 {
		t.Fatalf("mobs not joined: %+v", view.Runs)
	}
	// 7 s: a rotated run (refreshed every ~6 s) is still shown.
	c.Advance(7 * time.Second)
	view = s.Dungeons()
	if len(view.Runs) != 1 || view.Runs[0].Mobs == nil || len(*view.Runs[0].Mobs) != 1 {
		t.Fatalf("mobs dropped inside the staleness window: %+v", view.Runs)
	}
	// 9 s: past the 8 s limit, the entry and its member ids are gone.
	c.Advance(2 * time.Second)
	view = s.Dungeons()
	if len(view.Runs) != 1 {
		t.Fatalf("run lost with its mobs: %+v", view.Runs)
	}
	if view.Runs[0].Mobs != nil || view.Runs[0].MobsMap != nil {
		t.Fatalf("stale mobs still joined: %+v", view.Runs[0])
	}
	if view.Runs[0].Members[0].ID != "" || view.Runs[0].Members[0].Target != "" {
		t.Fatalf("stale member id still joined: %+v", view.Runs[0].Members[0])
	}
	if got := mobEntries(s); len(got) != 0 {
		t.Fatalf("stale entry not pruned: %+v", got)
	}
}

// The join: member ids/targets by low guid, the mob list on the run, and the
// reported-empty vs not-reported difference the page keys the layers off.
func TestDungeonsJoinsDungeonMobs(t *testing.T) {
	s := newTestStore(newClock())
	if ok := s.ApplyBatch(batch(1, 0, 1,
		du(1, "Tank", false, 409, 13),
		du(2, "Thralia", true, 409, 13))); !ok {
		t.Fatal("batch not published")
	}
	nowMobs := []model.CommanderMob{hostileMob(mobGUID, botGUID)}
	s.ApplyDungeonMobs(mobPayload(7, 5, mobRun(409, 13, []model.DungeonMobsMember{
		{GUID: 1, ID: botGUID, Target: mobGUID},
		{GUID: 2, ID: tankGUID},
	}, 57, nowMobs...)))

	view := s.Dungeons()
	if len(view.Runs) != 1 {
		t.Fatalf("runs: %+v", view.Runs)
	}
	run := view.Runs[0]
	if run.Mobs == nil || len(*run.Mobs) != 1 || (*run.Mobs)[0].ID != mobGUID {
		t.Fatalf("mobs: %+v", run.Mobs)
	}
	if run.MobsMap == nil || *run.MobsMap != 409 || run.MobsTotal != 57 {
		t.Fatalf("mobs map/total: %+v %d", run.MobsMap, run.MobsTotal)
	}
	byGUID := map[uint32]model.DungeonMember{}
	for _, m := range run.Members {
		byGUID[m.GUID] = m
	}
	if byGUID[1].ID != botGUID || byGUID[1].Target != mobGUID || byGUID[2].ID != tankGUID || byGUID[2].Target != "" {
		t.Fatalf("member ids/targets: %+v", run.Members)
	}

	// An empty list the module read is reported as "mobs":[] — the layers
	// stay selectable and old mobs vanish.
	s.ApplyDungeonMobs(mobPayload(7, 6, mobRun(409, 13, []model.DungeonMobsMember{{GUID: 1, ID: botGUID}}, 0)))
	view = s.Dungeons()
	if view.Runs[0].Mobs == nil || len(*view.Runs[0].Mobs) != 0 {
		t.Fatalf("reported-empty list not kept: %+v", view.Runs[0].Mobs)
	}
	out, err := json.Marshal(view.Runs[0])
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(out), `"mobs":[]`) {
		t.Fatalf("reported-empty list shape: %s", out)
	}
}

// A run without a payload (an older module) carries no mobs key at all; the
// JSON keeps a guid a string.
func TestDungeonRunWithoutMobsShape(t *testing.T) {
	s := newTestStore(newClock())
	if ok := s.ApplyBatch(batch(1, 0, 1, du(1, "Tank", false, 409, 13))); !ok {
		t.Fatal("batch not published")
	}
	out, err := json.Marshal(s.Dungeons().Runs[0])
	if err != nil {
		t.Fatal(err)
	}
	for _, key := range []string{`"mobs"`, `"mobs_map"`, `"mobs_total"`, `"id"`, `"target"`} {
		if strings.Contains(string(out), key) {
			t.Fatalf("old-module run carries %s: %s", key, out)
		}
	}

	// With data, the 64-bit guids stay decimal strings (a JSON number would
	// lose precision in the browser).
	s.ApplyDungeonMobs(mobPayload(7, 5, mobRun(409, 13, []model.DungeonMobsMember{{GUID: 1, ID: "10844", Target: mobGUID}}, 1, hostileMob(mobGUID, "10844"))))
	out, err = json.Marshal(s.Dungeons().Runs[0])
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(out), `"id":"`+mobGUID+`"`) || !strings.Contains(string(out), `"victim":"10844"`) {
		t.Fatalf("guid not a string: %s", out)
	}
}

// The wire shape the module writes (plan 2.5) unmarshals and marshals back
// with every field intact, including the part/parts hints and 64-bit ids.
func TestDungeonMobsWireShape(t *testing.T) {
	const wire = `{"v":12,"session":77,"seq":5,"ts":1791990010,"type":"DUNGEON_MOBS","part":0,"parts":2,"runs":[` +
		`{"map":389,"instance":7,"raid":false,"members":[{"guid":1234,"id":"1234","target":"` + mobGUID + `"}],` +
		`"mobs_map":389,"mobs_total":57,"mobs":[` +
		`{"id":"` + mobGUID + `","entry":4863,"name":"Stonevault Rockchewer","hp":85,"x":1230.5,"y":850,"victim":"1234","reaction":"h","lvl":46,"rank":"b","cc":"s","cast":"Frostbolt"},` +
		`{"id":"17379391094613082371","entry":4000,"name":"Rat","hp":100,"x":1240,"y":851,"reaction":"n","kind":"k"}]}]}`
	var p model.DungeonMobsPayload
	if err := json.Unmarshal([]byte(wire), &p); err != nil {
		t.Fatal(err)
	}
	if p.V != 12 || p.Session != 77 || p.Seq != 5 || p.Type != "DUNGEON_MOBS" || p.Part != 0 || p.Parts != 2 || len(p.Runs) != 1 {
		t.Fatalf("header: %+v", p)
	}
	r := p.Runs[0]
	if r.Map != 389 || r.Instance != 7 || r.Raid || r.MobsMap != 389 || r.MobsTotal != 57 || r.Mobs == nil || len(*r.Mobs) != 2 {
		t.Fatalf("run: %+v", r)
	}
	if m := (*r.Mobs)[0]; m.ID != mobGUID || m.Victim != "1234" || m.Rank != "b" || m.Cast != "Frostbolt" || m.CC != "s" || m.Level != 46 {
		t.Fatalf("mob: %+v", m)
	}
	out, err := json.Marshal(p)
	if err != nil {
		t.Fatal(err)
	}
	if string(out) != wire {
		t.Fatalf("round trip changed the shape:\n got %s\nwant %s", out, wire)
	}
}

// The live game server base (feat/dungeon-self-clear, what m910q runs) sends
// instance ids in DUNGEON_MOBS but not in BOT_BATCH, so the roster run is
// "43:0" while the mob entry is "43:102". Shortened from
// ~/TortoiseBots-audit/tests/live-check-1101/udp-capture2.log.
const liveMobsDatagram = `{"v":9,"session":1792493937,"seq":456,"ts":1792494861,"type":"DUNGEON_MOBS","part":0,"parts":1,"runs":[
 {"map":43,"instance":102,"raid":false,"members":[
   {"guid":713,"id":"713"},{"guid":712,"id":"712"},{"guid":710,"id":"710"},
   {"guid":711,"id":"711"},{"guid":709,"id":"709"},{"guid":643,"id":"643"}],
  "mobs_map":43,"mobs_total":24,"mobs":[
   {"id":"17379391058575656092","entry":5755,"name":"Deviate Viper","hp":100,"x":10.3,"y":457.6,"reaction":"h","lvl":19,"rank":"e"},
   {"id":"17379391026447287442","entry":3840,"name":"Druid of the Fang","hp":100,"x":-27.0,"y":420.6,"reaction":"h","lvl":19,"rank":"e"}]}]}`

func applyLiveMobs(t *testing.T, s *Store) {
	t.Helper()
	var p model.DungeonMobsPayload
	if err := json.Unmarshal([]byte(liveMobsDatagram), &p); err != nil {
		t.Fatal(err)
	}
	s.ApplyDungeonMobs(&p)
}

// duNoInstance is a roster row from a module base without #895: no instance
// field at all, so the run key falls back to "<map>:0".
func duNoInstance(guid uint32, name string, mapID uint32) model.BotSnapshot {
	return model.BotSnapshot{GUID: guid, Name: name, Kind: "pool", MapID: mapID, HP: 100, MaxHP: 100, State: "idle"}
}

// A roster that reports the instance joins by (map, instance), as before.
func TestDungeonMobsJoinByInstance(t *testing.T) {
	s := newTestStore(newClock())
	if ok := s.ApplyBatch(batch(1, 0, 1,
		du(712, "A", false, 43, 102), du(709, "B", false, 43, 102),
		du(710, "C", false, 43, 102), du(711, "D", false, 43, 102))); !ok {
		t.Fatal("batch not published")
	}
	applyLiveMobs(t, s)

	run := s.Dungeons().Runs[0]
	if run.Key != "43:102" || run.Mobs == nil || len(*run.Mobs) != 2 || run.MobsTotal != 24 || run.MobsMap == nil || *run.MobsMap != 43 {
		t.Fatalf("instance join: %+v", run)
	}
	for _, m := range run.Members {
		if m.ID != fmt.Sprint(m.GUID) {
			t.Fatalf("member id not joined: %+v", m)
		}
	}
}

// The live base reports no roster instance (run "43:0"): the entry joins by map
// and a shared member guid, and the stale limit still applies.
func TestDungeonMobsJoinWithoutRosterInstance(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	if ok := s.ApplyBatch(batch(1, 0, 1,
		duNoInstance(712, "A", 43), duNoInstance(709, "B", 43),
		duNoInstance(710, "C", 43), duNoInstance(711, "D", 43))); !ok {
		t.Fatal("batch not published")
	}
	applyLiveMobs(t, s)

	view := s.Dungeons()
	if !view.StaleModule || len(view.Runs) != 1 || view.Runs[0].Key != "43:0" {
		t.Fatalf("roster: %+v", view)
	}
	run := view.Runs[0]
	if run.Mobs == nil || len(*run.Mobs) != 2 || run.MobsTotal != 24 || run.MobsMap == nil || *run.MobsMap != 43 {
		t.Fatalf("shared-guid join: %+v", run)
	}
	for _, m := range run.Members {
		if m.ID != fmt.Sprint(m.GUID) {
			t.Fatalf("member id not joined: %+v", m)
		}
	}

	// Three cycles and more: the entry is stale and the roster run stays.
	c.Advance(9 * time.Second)
	run = s.Dungeons().Runs[0]
	if run.Mobs != nil || run.MobsMap != nil || run.Members[0].ID != "" {
		t.Fatalf("stale entry still joined: %+v", run)
	}
}

// The fallback matcher: by shared member guid only, on exactly one run, with
// the instance match taking precedence.
func TestMatchDungeonMobsBySharedMember(t *testing.T) {
	e := dungeonMobsEntry{run: model.DungeonMobsRun{Map: 43, Instance: 102,
		Members: []model.DungeonMobsMember{{GUID: 713}, {GUID: 712}, {GUID: 710}}}}
	entries := map[string]dungeonMobsEntry{"43:102": e}
	member := func(guid uint32) []model.DungeonMember { return []model.DungeonMember{{GUID: guid}} }

	// Two runs on the map, only one shares a member: the right one joins.
	runs := []model.DungeonRun{
		{Key: "43:0", MapID: 43, Instance: 0, Members: append(member(712), member(709)...)},
		{Key: "43:9", MapID: 43, Instance: 0, Members: member(999)},
	}
	got := matchDungeonMobs(runs, entries)
	if _, ok := got[0]; !ok || len(got) != 1 {
		t.Fatalf("shared-guid match: %+v", got)
	}

	// No shared member (one run on the map, so ambiguity cannot mask a
	// map-only match): never join by map alone.
	runs = []model.DungeonRun{
		{Key: "43:0", MapID: 43, Instance: 0, Members: member(999)},
	}
	if got = matchDungeonMobs(runs, entries); len(got) != 0 {
		t.Fatalf("map-only join: %+v", got)
	}

	// Two roster runs share a member with the entry: ambiguous, no join.
	runs = []model.DungeonRun{
		{Key: "43:0", MapID: 43, Instance: 0, Members: member(712)},
		{Key: "43:9", MapID: 43, Instance: 0, Members: member(713)},
	}
	if got = matchDungeonMobs(runs, entries); len(got) != 0 {
		t.Fatalf("ambiguous join: %+v", got)
	}

	// A known instance wins; the fallback run is not also joined.
	runs = []model.DungeonRun{
		{Key: "43:0", MapID: 43, Instance: 0, Members: member(712)},
		{Key: "43:102", MapID: 43, Instance: 102, Members: member(712)},
	}
	got = matchDungeonMobs(runs, entries)
	if _, ok := got[1]; !ok || len(got) != 1 {
		t.Fatalf("instance precedence: %+v", got)
	}
}

// A dungeon mob list carries the boss overlay too (the same object as the commander report), and the daemon notes when a
// cast ends without changing the caller's payload.
func TestDungeonMobsCastEnd(t *testing.T) {
	s := newTestStore(newClock())
	facing := 1.5
	boss := model.CommanderMob{ID: "5", Entry: 10184, Name: "Onyxia", HP: 80, Reaction: "h", Rank: "b", O: &facing,
		CastID: 18435, CastLeftMS: 1200, CastTotalMS: 2000, Shape: &model.CommanderShape{Kind: "cone", Angle: 120, Length: 45}}
	plain := hostileMob(mobGUID, botGUID)
	p := mobPayload(1, 1, mobRun(249, 7, nil, 2, boss, plain))
	p.TS = 100
	s.ApplyDungeonMobs(p)
	mobs := *mobEntries(s)["249:7"].run.Mobs
	if len(mobs) != 2 || mobs[0].CastEndMS != 101200 || mobs[0].Shape == nil || mobs[0].Shape.Kind != "cone" {
		t.Fatalf("boss overlay lost: %+v", mobs)
	}
	if mobs[1].CastEndMS != 0 || mobs[1].O != nil {
		t.Fatalf("an ordinary mob must carry no cast end: %+v", mobs[1])
	}
	if (*p.Runs[0].Mobs)[0].CastEndMS != 0 {
		t.Fatalf("the caller's payload must not be changed: %d", (*p.Runs[0].Mobs)[0].CastEndMS)
	}
}

// A payload carrying the millisecond time uses it for a cast's end: the whole-second TS is the older module's fallback.
func TestDungeonMobsCastEndPrefersTSMs(t *testing.T) {
	s := newTestStore(newClock())
	boss := model.CommanderMob{ID: "5", Entry: 10184, Name: "Onyxia", HP: 80, Reaction: "h", Rank: "b",
		CastID: 18435, CastLeftMS: 1200, CastTotalMS: 2000}
	p := mobPayload(1, 1, mobRun(249, 7, nil, 1, boss))
	p.TS, p.TSMs = 100, 100500
	s.ApplyDungeonMobs(p)
	mobs := *mobEntries(s)["249:7"].run.Mobs
	if mobs[0].CastEndMS != 101700 {
		t.Fatalf("CastEndMS = %d, want 101700 (TSMs 100500 + 1200, not TS 100 s)", mobs[0].CastEndMS)
	}
}
