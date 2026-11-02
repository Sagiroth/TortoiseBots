package combat

import "testing"

func mkRunBot(name string, guid uint32, role string) FightBot {
	return FightBot{Name: name, GUID: guid, Class: "warrior", Role: role, Sub: 0}
}

func TestRunTotalsSumsPerBot(t *testing.T) {
	f1 := Fight{ID: "f1", Commander: "G", Start: 1000, DurMS: 30000, Outcome: "kill"}
	f1.Bots = []FightBot{
		{Name: "Tanky", GUID: 1, Class: "warrior", Role: "tank", Sub: 0, Dmg: 1200, Taken: 3000, Casts: 6,
			Spells: []SpellCount{{N: "Sunder Armor", C: 6}, {N: "Taunt", C: 1}, {N: "Heroic Strike", C: 6}}},
		{Name: "Healz", GUID: 2, Class: "priest", Role: "healer", Sub: 0, Heal: 9000, Over: 1000, Casts: 20},
		{Name: "Pew", GUID: 3, Class: "mage", Role: "dps", Sub: 0, Dmg: 15000, Taken: 400, Casts: 30,
			Spells: []SpellCount{{N: "Fireball", C: 30}}},
	}
	f2 := Fight{ID: "f2", Commander: "G", Start: 2000, DurMS: 60000, Outcome: "kill"}
	f2.Bots = []FightBot{
		{Name: "Tanky", GUID: 1, Class: "warrior", Role: "tank", Sub: 0, Dmg: 800, Taken: 5000, Casts: 4,
			Spells: []SpellCount{{N: "Sunder Armor", C: 2}, {N: "Shield Slam", C: 4}}},
		{Name: "Healy", GUID: 2, Class: "priest", Role: "healer", Sub: 0, Heal: 3000, Over: 3000, Casts: 10},
		{Name: "New", GUID: 4, Class: "rogue", Role: "dps", Sub: 0, Dmg: 5000, Taken: 100, Casts: 5},
	}
	in := []RunInput{{Key: "k1", Fight: f1}, {Key: "k2", Fight: f2}}
	got, more := SumRun(in, RunTotalsCap)
	if more {
		t.Fatal("more set without a cut")
	}
	if got.Fights != 2 || got.DurMS != 90000 {
		t.Fatalf("totals = %+v, want 2 fights and 90000 ms", got)
	}
	byGUID := map[uint32]TotalBot{}
	for _, b := range got.Bots {
		byGUID[b.GUID] = b
	}
	tank := byGUID[1]
	if tank.Dmg != 2000 || tank.Taken != 8000 || tank.Casts != 10 || tank.FightMS != 90000 || tank.Fights != 2 {
		t.Errorf("tank = %+v", tank)
	}
	if tank.Spells["Sunder Armor"] != 8 || tank.Spells["Taunt"] != 1 {
		t.Errorf("tank spells = %+v, want Sunder 8 and Taunt 1", tank.Spells)
	}
	if _, ok := tank.Spells["Heroic Strike"]; ok {
		t.Errorf("tank spells = %+v, want only the Tank table's Sunder and taunts", tank.Spells)
	}
	heal := byGUID[2]
	if heal.Heal != 12000 || heal.Over != 4000 || heal.Casts != 30 || heal.FightMS != 90000 {
		t.Errorf("healer = %+v", heal)
	}
	if heal.Name != "Healy" {
		t.Errorf("healer name = %q, want the latest fight's", heal.Name)
	}
	if byGUID[3].FightMS != 30000 || byGUID[3].Dmg != 15000 {
		t.Errorf("pew = %+v, want only the one fight's time and damage", byGUID[3])
	}
	if byGUID[4].Name != "New" || byGUID[4].Role != "dps" || byGUID[4].Class != "rogue" {
		t.Errorf("new = %+v", byGUID[4])
	}
}

// The same fight filed twice (memory and history hold the same key) is summed once.
func TestRunTotalsDedupsByKey(t *testing.T) {
	f := Fight{ID: "f1", Commander: "G", Start: 1000, DurMS: 30000, Outcome: "kill"}
	f.Bots = []FightBot{mkRunBot("Tanky", 1, "tank")}
	f.Bots[0].Dmg = 1200
	in := []RunInput{{Key: "k1", Fight: f}, {Key: "k1", Fight: f}}
	got, _ := SumRun(in, RunTotalsCap)
	if got.Fights != 1 || got.DurMS != 30000 || len(got.Bots) != 1 || got.Bots[0].Dmg != 1200 {
		t.Fatalf("totals = %+v, want the fight summed once", got)
	}
}

// A fight still running (the live row) or filed unfinished carries no per-bot numbers and is not summed.
func TestRunTotalsSkipsLive(t *testing.T) {
	live := Fight{ID: "open", Commander: "G", Start: 3000, DurMS: 5000, Outcome: "live"}
	unfin := Fight{ID: "unfin", Commander: "G", Start: 2500, DurMS: 4000, Outcome: "unfinished"}
	done := Fight{ID: "f1", Commander: "G", Start: 1000, DurMS: 30000, Outcome: "kill"}
	got, _ := SumRun([]RunInput{{Key: "k0", Fight: live}, {Key: "ku", Fight: unfin}, {Key: "k1", Fight: done}}, RunTotalsCap)
	if got.Fights != 1 || got.DurMS != 30000 {
		t.Fatalf("totals = %+v, want only the finished fight", got)
	}
}

// Past the cap only the newest fights are summed, and more reports the cut.
func TestRunTotalsCap(t *testing.T) {
	var in []RunInput
	for i := int64(0); i < 5; i++ {
		f := Fight{ID: string(rune('a' + i)), Commander: "G", Start: 1000 + i, DurMS: 10000, Outcome: "kill"}
		f.Bots = []FightBot{mkRunBot("Tanky", 1, "tank")}
		f.Bots[0].Dmg = uint64(1000 + i)
		in = append(in, RunInput{Key: "k" + f.ID, Fight: f})
	}
	got, more := SumRun(in, 2)
	if !more {
		t.Fatal("more not set when the cap cut fights")
	}
	if got.Fights != 2 || got.Bots[0].Dmg != 1003+1004 {
		t.Fatalf("totals = %+v, want the two newest fights summed", got)
	}
	if _, more := SumRun(in, 5); more {
		t.Fatal("more set when nothing was cut")
	}
}
