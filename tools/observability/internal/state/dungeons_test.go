package state

import (
	"encoding/json"
	"strings"
	"testing"
	"time"

	"tortoise-observability/internal/model"
)

// du builds one roster row in the open world by default.
func du(guid uint32, name string, player bool, mapID, instance uint32) model.BotSnapshot {
	inst := instance
	return model.BotSnapshot{
		GUID: guid, Name: name, Player: player, Kind: "pool",
		MapID: mapID, Instance: &inst,
		HP: 100, MaxHP: 100, State: "idle",
	}
}

func runKeys(runs map[string][]model.BotSnapshot) []string {
	out := make([]string, 0, len(runs))
	for k := range runs {
		out = append(out, k)
	}
	return out
}

func hasRun(t *testing.T, runs map[string][]model.BotSnapshot, key string, wantMembers int) {
	t.Helper()
	members, ok := runs[key]
	if !ok {
		t.Fatalf("expected run %q in %v", key, runKeys(runs))
	}
	if len(members) != wantMembers {
		t.Fatalf("run %q: got %d members, want %d", key, len(members), wantMembers)
	}
}

// Two copies of the same dungeon (same map, different instance id) stay two
// separate runs.
func TestTwoCopiesStaySeparate(t *testing.T) {
	runs := GroupDungeonRuns([]model.BotSnapshot{
		du(1, "Tank", false, 409, 13),
		du(2, "Heal", false, 409, 13),
		du(3, "Tank2", false, 409, 14),
	})
	if len(runs) != 2 {
		t.Fatalf("got %d runs, want 2: %v", len(runs), runKeys(runs))
	}
	hasRun(t, runs, "409:13", 2)
	hasRun(t, runs, "409:14", 1)
}

// The real player of a group runs with its bots in one run.
func TestPlayerAndBotsOneRun(t *testing.T) {
	runs := GroupDungeonRuns([]model.BotSnapshot{
		du(1, "Thralia", true, 70, 5),
		du(2, "Tank", false, 70, 5),
		du(3, "Heal", false, 70, 5),
	})
	hasRun(t, runs, "70:5", 3)
}

// The open world (instance 0) and battlegrounds are never runs.
func TestOpenWorldAndBattlegroundsExcluded(t *testing.T) {
	openWorld := du(1, "Grinder", false, 1, 0)
	bg := du(2, "BgBot", false, 489, 7)
	bg.Pvp = "bg"
	bgPlayer := du(3, "BgPlayer", true, 489, 7)
	bgPlayer.Pvp = "bg"
	runs := GroupDungeonRuns([]model.BotSnapshot{openWorld, bg, bgPlayer})
	if len(runs) != 0 {
		t.Fatalf("got runs %v, want none", runKeys(runs))
	}
}

// A player alone in an instance (no bot with it) is no run.
func TestPlayerAloneNoRun(t *testing.T) {
	runs := GroupDungeonRuns([]model.BotSnapshot{du(1, "Solo", true, 33, 2)})
	if len(runs) != 0 {
		t.Fatalf("got runs %v, want none", runKeys(runs))
	}
}

// A pre-v12 module (no instance field) falls back to one run per map: its
// dungeon copies all appear, as one run per map.
func TestMissingInstanceFallsBackToMapRun(t *testing.T) {
	old := model.BotSnapshot{GUID: 1, Name: "Tank", MapID: 409, HP: 100, MaxHP: 100, State: "idle"}
	oldWorld := model.BotSnapshot{GUID: 2, Name: "Grinder", MapID: 1, HP: 100, MaxHP: 100, State: "idle"}
	runs := GroupDungeonRuns([]model.BotSnapshot{old, oldWorld})
	if len(runs) != 1 {
		t.Fatalf("got %d runs, want 1: %v", len(runs), runKeys(runs))
	}
	hasRun(t, runs, "409:0", 1)
}

// Store side: a run first seen at one publish keeps its first-seen time on
// later publishes, a run that is gone is forgotten, and the view says when
// the module reports no instance field at all.
func TestDungeonFirstSeenAndStaleModule(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	if ok := s.ApplyBatch(batch(1, 0, 1, du(1, "Tank", false, 409, 13))); !ok {
		t.Fatal("batch 1 not published")
	}
	view := s.Dungeons()
	if len(view.Runs) != 1 || view.Runs[0].Key != "409:13" || view.Runs[0].Instance != 13 {
		t.Fatalf("unexpected view: %+v", view)
	}
	if view.Runs[0].FirstSeen == 0 || view.Runs[0].SeenSec < 0 {
		t.Fatalf("first seen not stamped: %+v", view.Runs[0])
	}
	first := view.Runs[0].FirstSeen

	c.Advance(4 * time.Second)
	if ok := s.ApplyBatch(batch(2, 0, 1, du(1, "Tank", false, 409, 13))); !ok {
		t.Fatal("batch 2 not published")
	}
	view = s.Dungeons()
	if view.Runs[0].FirstSeen != first {
		t.Fatalf("first seen moved: %d -> %d", first, view.Runs[0].FirstSeen)
	}
	if view.Runs[0].SeenSec < 4 {
		t.Fatalf("seen_sec = %f, want >= 4", view.Runs[0].SeenSec)
	}

	c.Advance(2 * time.Second)
	if ok := s.ApplyBatch(batch(3, 0, 1, du(1, "Grinder", false, 1, 0))); !ok {
		t.Fatal("batch 3 not published")
	}
	if view = s.Dungeons(); len(view.Runs) != 0 {
		t.Fatalf("run left behind after the roster moved out: %+v", view.Runs)
	}

	// A pre-v12 module: the run falls back to map 409:0 and the view says so.
	s2 := newTestStore(c)
	old := model.BotSnapshot{GUID: 1, Name: "Tank", MapID: 409, HP: 100, MaxHP: 100, State: "idle"}
	if ok := s2.ApplyBatch(batch(1, 0, 1, old)); !ok {
		t.Fatal("old batch not published")
	}
	if view = s2.Dungeons(); !view.StaleModule || len(view.Runs) != 1 || view.Runs[0].Instance != 0 {
		t.Fatalf("stale module not reported: %+v", view)
	}
}

// Each run is listed once in the served view, however many members it has
// (two copies of one dungeon, a player and a bot in each).
func TestDungeonsViewListsEachRunOnce(t *testing.T) {
	s := newTestStore(newClock())
	if ok := s.ApplyBatch(batch(1, 0, 1,
		du(1, "PlayerA", true, 36, 102), du(2, "BotA", false, 36, 102),
		du(3, "PlayerB", true, 36, 103), du(4, "BotB", false, 36, 103))); !ok {
		t.Fatal("batch not published")
	}
	view := s.Dungeons()
	if len(view.Runs) != 2 || view.Runs[0].Key != "36:102" || view.Runs[1].Key != "36:103" {
		t.Fatalf("want runs 36:102 and 36:103 once each, got %+v", view.Runs)
	}
	for _, run := range view.Runs {
		if run.Players != 1 || run.Bots != 1 || len(run.Members) != 2 {
			t.Fatalf("run %s: players %d bots %d members %d", run.Key, run.Players, run.Bots, len(run.Members))
		}
	}
}

// The party rows carry the roster's power (issue #1112) — current, max and
// type, the fields the Bots tab's bar reads — and leave the keys out when
// the module reports none, so an old module draws no bar.
func TestDungeonMembersCarryPower(t *testing.T) {
	s := newTestStore(newClock())
	caster := du(1, "Caster", false, 409, 13)
	caster.Power, caster.MaxPower, caster.PowerType = 1183, 1183, "mana"
	empty := du(2, "Rager", false, 409, 13)
	empty.Power, empty.MaxPower, empty.PowerType = 0, 100, "rage" // an empty bar is still a bar
	old := du(3, "OldTimer", false, 409, 13)                      // a pre-power module row
	if ok := s.ApplyBatch(batch(1, 0, 1, caster, empty, old)); !ok {
		t.Fatal("batch not published")
	}
	view := s.Dungeons()
	if len(view.Runs) != 1 || len(view.Runs[0].Members) != 3 {
		t.Fatalf("unexpected runs: %+v", view.Runs)
	}
	byName := map[string]model.DungeonMember{}
	for _, m := range view.Runs[0].Members {
		byName[m.Name] = m
	}
	if m := byName["Caster"]; m.Power == nil || *m.Power != 1183 || m.MaxPower == nil || *m.MaxPower != 1183 || m.PowerType != "mana" {
		t.Fatalf("Caster power not carried: %+v", m)
	}
	if m := byName["Rager"]; m.Power == nil || *m.Power != 0 || m.MaxPower == nil || *m.MaxPower != 100 || m.PowerType != "rage" {
		t.Fatalf("an empty rage bar must still be present: %+v", m)
	}
	if m := byName["OldTimer"]; m.Power != nil || m.MaxPower != nil || m.PowerType != "" {
		t.Fatalf("a row without power must carry none: %+v", m)
	}

	raw, err := json.Marshal(view)
	if err != nil {
		t.Fatalf("marshal view: %v", err)
	}
	// The old row's JSON has no power keys at all (what the tab keys "no bar" on).
	var decoded struct {
		Runs []struct {
			Members []map[string]any `json:"members"`
		} `json:"runs"`
	}
	if err := json.Unmarshal(raw, &decoded); err != nil {
		t.Fatalf("unmarshal view: %v", err)
	}
	members := map[string]map[string]any{}
	for _, m := range decoded.Runs[0].Members {
		members[m["name"].(string)] = m
	}
	if members["Caster"]["power"] != float64(1183) || members["Caster"]["max_power"] != float64(1183) || members["Caster"]["power_type"] != "mana" {
		t.Fatalf("Caster JSON power: %v", members["Caster"])
	}
	if members["Rager"]["power"] != float64(0) || members["Rager"]["max_power"] != float64(100) {
		t.Fatalf("Rager JSON power: %v", members["Rager"])
	}
	oldJSON, _ := json.Marshal(members["OldTimer"])
	for _, key := range []string{"power", "max_power", "power_type"} {
		if strings.Contains(string(oldJSON), `"`+key+`"`) {
			t.Fatalf("old module member JSON carries %q: %s", key, oldJSON)
		}
	}
}
// A raid copy (any member's roster `raid` flag) never reaches the Dungeons
// view; without any flag (an old module) both copies stay dungeons. The
// dungeon copy holds 10 members and the raid copy 5, so a size threshold
// would sort them the wrong way round.
func TestDungeonsExcludesRaidRuns(t *testing.T) {
	dng := func(guid uint32, mapID, instance uint32) model.BotSnapshot {
		return du(guid, "Dng", false, mapID, instance)
	}
	raid := func(guid uint32) model.BotSnapshot {
		b := du(guid, "Raid", false, 409, 7)
		b.Raid = true
		return b
	}
	members := make([]model.BotSnapshot, 0, 15)
	for g := uint32(1); g <= 10; g++ {
		members = append(members, dng(g, 36, 102))
	}
	for g := uint32(11); g <= 15; g++ {
		members = append(members, raid(g))
	}
	s := newTestStore(newClock())
	if ok := s.ApplyBatch(batch(1, 0, 1, members...)); !ok {
		t.Fatal("batch not published")
	}
	view := s.Dungeons()
	if len(view.Runs) != 1 || view.Runs[0].Key != "36:102" {
		t.Fatalf("only the dungeon copy must list: %+v", view.Runs)
	}

	// An old module sends no flag: both copies stay dungeons.
	s2 := newTestStore(newClock())
	plain := members
	for i := range plain {
		plain[i].Raid = false
	}
	if ok := s2.ApplyBatch(batch(1, 0, 1, plain...)); !ok {
		t.Fatal("batch not published")
	}
	if view = s2.Dungeons(); len(view.Runs) != 2 {
		t.Fatalf("old module: both copies stay: %+v", view.Runs)
	}
}
