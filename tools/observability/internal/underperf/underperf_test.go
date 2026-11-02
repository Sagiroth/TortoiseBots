package underperf

import (
	"encoding/json"
	"math"
	"testing"

	"tortoise-observability/internal/combat"
)

// tb makes one bot row of a fight. Spells default to nil (a full list of the
// bot's casts at the cap would mark it cut; the tests that care pass spells).
func tb(guid uint32, name, class, role string, dmg, heal uint64, casts, healCasts, deaths uint32) combat.FightBot {
	return combat.FightBot{
		Name: name, GUID: guid, Class: class, Role: role,
		Dmg: dmg, Heal: heal, Casts: casts, HealCasts: healCasts, Deaths: deaths,
	}
}

// fi builds one input fight at start with durMS.
func fi(key string, start int64, durMS uint32, bots ...combat.FightBot) Input {
	return Input{
		Key:      key,
		Day:      "2026-11-01",
		Fight:    combat.Fight{ID: key, Commander: "Tacmdbilo", Start: start, DurMS: durMS, Outcome: "end", Bots: bots},
		Warnings: nil,
	}
}

func defLim() Limits { return DefaultLimits() }

func findBot(t *testing.T, r Report, name string) *BotRow {
	t.Helper()
	for i := range r.Bots {
		if r.Bots[i].Name == name {
			return &r.Bots[i]
		}
	}
	return nil
}

func findReason(row *BotRow, id string) *Reason {
	for i := range row.Reasons {
		if row.Reasons[i].ID == id {
			return &row.Reasons[i]
		}
	}
	return nil
}

func fv(v *float64) float64 {
	if v == nil {
		return math.NaN()
	}
	return *v
}

func roundEq(a, b float64) bool {
	d := a - b
	return d < 1e-9 && d > -1e-9
}

func TestLowDpsFlaggedAgainstRolePeers(t *testing.T) {
	var fights []Input
	for i := range 3 {
		fights = append(fights, fi("f"+string(rune('a'+i)), int64(1000+i*100), 30000,
			tb(1, "TankA", "warrior", "tank", 0, 0, 10, 0, 0),
			tb(2, "TankB", "warrior", "tank", 0, 0, 10, 0, 0),
			tb(3, "HealA", "priest", "healer", 0, 3000, 0, 10, 0),
			tb(4, "HealB", "druid", "healer", 0, 3000, 0, 10, 0),
			tb(5, "Lowbie", "rogue", "dps", 300, 0, 10, 0, 0),
			tb(6, "DpsB", "mage", "dps", 3000, 0, 10, 0, 0),
			tb(7, "DpsC", "hunter", "dps", 3000, 0, 10, 0, 0),
			tb(8, "DpsD", "warlock", "dps", 3000, 0, 10, 0, 0),
			tb(9, "DpsE", "rogue", "dps", 3300, 0, 10, 0, 0),
			tb(10, "DpsF", "warrior", "dps", 2700, 0, 10, 0, 0),
		))
	}
	r := Build("Tacmdbilo", 0, 1<<62, fights, nil, Floors{}, defLim())
	if len(r.Bots) != 1 {
		t.Fatalf("bots = %d rows, want 1 (only Lowbie): %+v", len(r.Bots), r.Bots)
	}
	row := &r.Bots[0]
	if row.Name != "Lowbie" {
		t.Fatalf("listed %q, want Lowbie", row.Name)
	}
	if row.Level != "red" {
		t.Errorf("level = %q, want red", row.Level)
	}
	if !roundEq(fv(row.You.Dps), 10) {
		t.Errorf("you.dps = %v, want 10", row.You.Dps)
	}
	if !roundEq(fv(row.Peers.Dps), 100) {
		t.Errorf("peers.dps = %v, want 100", row.Peers.Dps)
	}
	if !roundEq(fv(row.Ratio.Main), 0.1) {
		t.Errorf("ratio.main = %v, want 0.1", row.Ratio.Main)
	}
	if row.PeerFights != 3 || row.Flagged != 3 {
		t.Errorf("peer_fights = %d, flagged = %d, want 3 and 3", row.PeerFights, row.Flagged)
	}
	if len(row.FightRefs) != 3 {
		t.Fatalf("fight_refs = %d, want 3", len(row.FightRefs))
	}
	for _, ref := range row.FightRefs {
		if !ref.Low {
			t.Errorf("fight_ref %q low = false, want true", ref.Key)
		}
	}
	if r := findReason(row, "peer:main"); r == nil || r.Level != "red" {
		t.Errorf("peer:main reason = %+v, want red", r)
	}
	if len(r.OK) != 9 {
		t.Errorf("ok = %d bots, want 9", len(r.OK))
	}
	if r.Fights != 3 || r.Judged != 3 {
		t.Errorf("fights = %d, judged = %d, want 3 and 3", r.Fights, r.Judged)
	}
	if _, err := json.Marshal(r); err != nil {
		t.Fatalf("marshal: %v", err)
	}
}
