package combat

import (
	"testing"

	"tortoise-observability/internal/model"
)

// parked is the commander snapshot of an Onyxia stand: a live window of heals and buff casts, no damage, and the
// warders near the lead not engaged with anyone.
func parked() model.CommanderBinding {
	return model.CommanderBinding{
		Commander: "Glacienne",
		Bots: []model.CommanderBot{
			{Name: "Tanky", GUID: 813, ID: "813", Role: "tank", Alive: true},
			{Name: "Healy", GUID: 814, ID: "814", Role: "healer", Alive: true},
		},
		Mobs: []model.CommanderMob{
			{ID: "17379391165513648980", Entry: 12129, Name: "Onyxian Warder", Reaction: "h"},
		},
		Combat: &model.CommanderCombat{
			WinS:    10,
			FightMS: 173*60000 + 23000,
			Bots: []model.CommanderCombatBot{
				{GUID: 814, DPS: 0, HPS: 250, Casts: 3, Last: "Power Word: Fortitude"},
			},
		},
	}
}

func TestEngagedParked(t *testing.T) {
	if Engaged(parked()) {
		t.Fatal("a parked raid buffing and healing is not an engaged fight")
	}
}

func TestEngagedByHostileVictim(t *testing.T) {
	b := parked()
	b.Mobs[0].Victim = "813"
	if !Engaged(b) {
		t.Fatal("a hostile mob whose victim is a group member is engagement")
	}
	// A victim that is no group member (a pet outside the roster, a stranger) is not the group's fight.
	b.Mobs[0].Victim = "999"
	if Engaged(b) {
		t.Fatal("a hostile mob targeting a non-member is not engagement")
	}
	// Only a hostile unit counts: a neutral mob's victim is not the fight this gate is for.
	b.Mobs[0].Reaction = "n"
	b.Mobs[0].Victim = "813"
	if Engaged(b) {
		t.Fatal("a non-hostile mob is not engagement")
	}
}

func TestEngagedByDamageInWindow(t *testing.T) {
	b := parked()
	b.Mobs = nil
	if Engaged(b) {
		t.Fatal("a window without damage is not engagement")
	}
	b.Combat.Bots[0].DPS = 42
	if !Engaged(b) {
		t.Fatal("damage dealt to a hostile in the 10 s window is engagement")
	}
}

func TestEngagedWithoutLiveWindow(t *testing.T) {
	b := parked()
	b.Combat = nil
	b.Mobs[0].Victim = "813"
	if Engaged(b) {
		t.Fatal("no live window: no fight")
	}
}

// The live row of a parked group is kept out of the fights list and the SSE snapshot, and comes back the moment a
// hostile is engaged; no engagement again (the mob died or evaded and the 10 s window emptied) takes it away.
func TestLiveRowListedOnlyWhileEngaged(t *testing.T) {
	s := newLiveStore(t)
	engaged := false
	s.SetEngaged(func(commander string) bool { return commander == "Gbmage" && engaged })
	s.ApplyEvents(liveEvents(liveHeader(), `{"k":"start","t":0}`,
		`{"k":"cast","t":1000,"id":"1","e":2,"n":"Bot","s":"Arcane Intellect","r":"b"}`), liveNow)

	if list := s.List("Gbmage"); len(list) != 0 {
		t.Fatalf("a parked group must not list a live fight: %+v", list)
	}
	if rows := s.LiveRows(nil); len(rows) != 0 {
		t.Fatalf("nor push one to the live stream: %+v", rows)
	}

	engaged = true
	list := s.List("Gbmage")
	if len(list) != 1 || !list[0].Live || list[0].ID != "55-6" {
		t.Fatalf("an engaged group lists its live fight: %+v", list)
	}
	if rows := s.LiveRows(nil); len(rows) != 1 || !rows[0].Live {
		t.Fatalf("and pushes it: %+v", rows)
	}

	engaged = false
	if list := s.List("Gbmage"); len(list) != 0 {
		t.Fatalf("no hostile engaged for 10 s: the live fight closes: %+v", list)
	}
	if rows := s.LiveRows(nil); len(rows) != 0 {
		t.Fatalf("live stream after the close: %+v", rows)
	}
}

// The row is hidden, not filed: the module's book may still end in a COMBAT_FIGHT, and that fight must be filed and
// listed once, never twice.
func TestParkedLiveRowStillFiles(t *testing.T) {
	s := newLiveStore(t)
	s.SetEngaged(func(string) bool { return false })
	s.ApplyEvents(liveEvents(liveHeader(), `{"k":"start","t":0}`, deathEvent(1200, "Tanky")), liveNow)

	f := Fight{ID: "55-6", Commander: "Gbmage", Group: 55, Map: 409, Start: liveNow.Unix() - 30, DurMS: 30000,
		Outcome: "wipe", BossName: "Magmadar", Deaths: []FightDeath{{Name: "Tanky", TMs: 1200}}}
	if !s.Add(7, f) {
		t.Fatal("the finished fight must file even while the live row is hidden")
	}
	list := s.List("Gbmage")
	if len(list) != 1 || list[0].Live || list[0].Outcome != "wipe" {
		t.Fatalf("the finished fight stands alone: %+v", list)
	}
}

// No check installed (an older daemon/test wiring): every open row is listed as before.
func TestLiveRowsWithoutEngagedCheck(t *testing.T) {
	s := newLiveStore(t)
	s.ApplyEvents(liveEvents(liveHeader(), `{"k":"start","t":0}`), liveNow)
	if list := s.List("Gbmage"); len(list) != 1 || !list[0].Live {
		t.Fatalf("a store without the check lists its open row: %+v", list)
	}
}
