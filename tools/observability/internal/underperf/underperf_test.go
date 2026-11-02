package underperf

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

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

func TestPeerMedianExcludesTheBot(t *testing.T) {
	var fights []Input
	for i := range 3 {
		fights = append(fights, fi("f"+itoa(i), int64(1000+i*100), 30000,
			tb(1, "A", "rogue", "dps", 450, 0, 10, 0, 0),
			tb(2, "B", "mage", "dps", 500, 0, 10, 0, 0),
			tb(3, "C", "hunter", "dps", 1000, 0, 10, 0, 0),
			tb(4, "D", "warlock", "dps", 1000, 0, 10, 0, 0),
		))
	}
	r := Build("Tacmdbilo", 0, 1<<62, fights, nil, Floors{}, defLim())
	if len(r.Bots) != 1 || r.Bots[0].Name != "A" || r.Bots[0].Level != "yellow" {
		t.Fatalf("bots = %+v, want only A yellow", r.Bots)
	}
	if !roundEq(fv(r.Bots[0].Ratio.Main), 0.45) {
		t.Errorf("ratio.main = %v, want 0.45", fv(r.Bots[0].Ratio.Main))
	}
}

func TestAliveTimeNotFightLength(t *testing.T) {
	var fights []Input
	for i := range 2 {
		f := fi("f"+itoa(i), int64(1000+i*100), 60000,
			tb(1, "A", "rogue", "dps", 1200, 0, 10, 0, 1),
			tb(2, "B", "mage", "dps", 6000, 0, 10, 0, 0),
			tb(3, "C", "hunter", "dps", 6000, 0, 10, 0, 0),
		)
		f.Fight.Deaths = []combat.FightDeath{{Name: "A", TMs: 12000}}
		fights = append(fights, f)
	}
	r := Build("Tacmdbilo", 0, 1<<62, fights, nil, Floors{}, defLim())
	if len(r.Bots) != 0 {
		t.Fatalf("bots = %+v, want none (A's rate over alive time equals its peers')", r.Bots)
	}
	if len(r.OK) != 3 {
		t.Fatalf("ok = %+v, want all three", r.OK)
	}
}

func TestDiedAtOnceNotJudged(t *testing.T) {
	var fights []Input
	for i := range 3 {
		f := fi("f"+itoa(i), int64(1000+i*100), 30000,
			tb(1, "Dead", "rogue", "dps", 0, 0, 0, 0, 1),
			tb(2, "B", "mage", "dps", 3000, 0, 10, 0, 0),
			tb(3, "C", "hunter", "dps", 3000, 0, 10, 0, 0),
			tb(4, "D", "warlock", "dps", 3000, 0, 10, 0, 0),
		)
		f.Fight.Deaths = []combat.FightDeath{{Name: "Dead", TMs: 5000}}
		f.Warnings = []combat.Warning{{Rule: "dps_no_damage", Level: "red", Bot: "Dead", Role: "dps", Text: "Dead did no damage"}}
		fights = append(fights, f)
	}
	r := Build("Tacmdbilo", 0, 1<<62, fights, nil, Floors{}, defLim())
	if len(r.Bots) != 1 || r.Bots[0].Name != "Dead" {
		t.Fatalf("bots = %+v, want only Dead (by the rule warning)", r.Bots)
	}
	row := &r.Bots[0]
	if row.Judged != 0 {
		t.Errorf("judged = %d, want 0", row.Judged)
	}
	for name, v := range map[string]*float64{"dps": row.You.Dps, "hps": row.You.Hps, "hcps": row.You.Hcps, "cps": row.You.Cps, "skill10": row.You.Skill10} {
		if v != nil {
			t.Errorf("you.%s = %v, want null", name, *v)
		}
	}
	for _, reason := range row.Reasons {
		if len(reason.ID) >= 5 && reason.ID[:5] == "peer:" {
			t.Errorf("peer reason %q on a bot with no judged fight", reason.ID)
		}
	}
	if _, err := json.Marshal(r); err != nil {
		t.Fatalf("marshal: %v", err)
	}
}

func TestNoFights(t *testing.T) {
	r := Build("Tacmdbilo", 100, 200, nil, nil, Floors{}, defLim())
	b, err := json.Marshal(r)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	s := string(b)
	if r.Fights != 0 || r.Judged != 0 {
		t.Errorf("fights = %d, judged = %d, want 0 and 0", r.Fights, r.Judged)
	}
	for _, key := range []string{`"bots":[]`, `"ok":[]`, `"runs":[]`} {
		if !strings.Contains(s, key) {
			t.Errorf("json %s missing %s", s, key)
		}
	}
}

func TestBotWithNoJudgedFights(t *testing.T) {
	var fights []Input
	for i := range 2 {
		fights = append(fights, fi("f"+itoa(i), int64(1000+i*100), 15000,
			tb(1, "Short", "rogue", "dps", 300, 0, 2, 0, 0),
			tb(2, "B", "mage", "dps", 300, 0, 2, 0, 0),
			tb(3, "C", "hunter", "dps", 300, 0, 2, 0, 0),
		))
	}
	r := Build("Tacmdbilo", 0, 1<<62, fights, nil, Floors{}, defLim())
	if len(r.Bots) != 0 {
		t.Fatalf("bots = %+v, want none", r.Bots)
	}
	if len(r.OK) != 3 {
		t.Fatalf("ok = %+v, want all three", r.OK)
	}
	for _, ok := range r.OK {
		if ok.Judged != 0 {
			t.Errorf("%s judged = %d, want 0", ok.Name, ok.Judged)
		}
	}
	if _, err := json.Marshal(r); err != nil {
		t.Fatalf("marshal: %v", err)
	}
}

func rulesOf(t *testing.T) []combat.Rule {
	t.Helper()
	rules, err := combat.LoadRules(combat.DefaultRules())
	if err != nil {
		t.Fatalf("rules: %v", err)
	}
	return rules
}

func TestFivePersonParty(t *testing.T) {
	rules := rulesOf(t)
	var fights []Input
	for i := range 3 {
		f := fi("f"+itoa(i), int64(1000+i*100), 60000,
			tb(1, "Tank", "warrior", "tank", 0, 0, 20, 0, 0),
			tb(2, "Healer", "priest", "healer", 0, 0, 0, 0, 0),
			tb(3, "Low", "rogue", "dps", 300, 0, 10, 0, 0),
			tb(4, "DpsB", "mage", "dps", 3000, 0, 10, 0, 0),
			tb(5, "DpsC", "hunter", "dps", 3000, 0, 10, 0, 0),
		)
		f.Fight.Bots[0].Spells = []combat.SpellCount{{N: "Sunder Armor", C: 20}}
		f.Fight.Bots[0].Taken = 5000 // the lone healer's idle test needs real damage taken
		f.Warnings = combat.Judge(f.Fight, rules)
		fights = append(fights, f)
	}
	r := Build("Tacmdbilo", 0, 1<<62, fights, rules, ParseFloors(rules), defLim())
	healer := findBot(t, r, "Healer")
	if healer == nil || healer.Level != "red" || findReason(healer, "rule:healer_no_heals") == nil {
		t.Fatalf("healer = %+v, want red with rule:healer_no_heals", healer)
	}
	if healer.Peers.Hps != nil {
		t.Errorf("healer peers.hps = %v, want null (no peer healer)", *healer.Peers.Hps)
	}
	if findBot(t, r, "Tank") != nil {
		t.Errorf("tank listed; it has no peer and no rule")
	}
	low := findBot(t, r, "Low")
	if low == nil || findReason(low, "peer:main") == nil {
		t.Fatalf("Low = %+v, want peer:main", low)
	}
}

func TestFortyRaid(t *testing.T) {
	var fights []Input
	for i := range 3 {
		var bots []combat.FightBot
		for g := range 4 {
			bots = append(bots, tb(uint32(100+g), "Tank"+itoa(g), "warrior", "tank", 0, 0, 20, 0, 0))
		}
		for g := range 8 {
			heal := uint64(3000)
			if g == 0 {
				heal = 600
			}
			bots = append(bots, tb(uint32(200+g), "Heal"+itoa(g), "priest", "healer", 0, heal, 0, 20, 0))
		}
		for g := range 28 {
			bots = append(bots, tb(uint32(300+g), "Dps"+itoa(g), "mage", "dps", 3000, 0, 10, 0, 0))
		}
		fights = append(fights, fi("f"+itoa(i), int64(1000+i*100), 30000, bots...))
	}
	r := Build("Tacmdbilo", 0, 1<<62, fights, nil, Floors{}, defLim())
	if len(r.Bots) != 1 {
		t.Fatalf("bots = %d, want only Heal0", len(r.Bots))
	}
	if r.Bots[0].Name != "Heal0" || r.Bots[0].Level != "red" {
		t.Fatalf("row = %+v, want Heal0 red", r.Bots[0])
	}
}

func TestTwoTanksCompare(t *testing.T) {
	var fights []Input
	for i := range 3 {
		fights = append(fights, fi("f"+itoa(i), int64(1000+i*100), 30000,
			tb(1, "Slow", "warrior", "tank", 0, 0, 3, 0, 0),
			tb(2, "Fast", "warrior", "tank", 0, 0, 15, 0, 0),
		))
	}
	r := Build("Tacmdbilo", 0, 1<<62, fights, nil, Floors{}, defLim())
	slow := findBot(t, r, "Slow")
	if slow == nil || slow.Level != "red" || findReason(slow, "peer:main") == nil {
		t.Fatalf("Slow = %+v, want red peer:main at MinTankPeers 1", slow)
	}
	if findBot(t, r, "Fast") != nil {
		t.Errorf("Fast listed, want only Slow")
	}

	// One guid in two roles is two rows: the key is guid and role, not the name
	// (a bot that healed in one fight and damaged in another).
	fights = append(fights,
		fi("s1", 5000, 30000, tb(9, "Switch", "priest", "dps", 3000, 0, 10, 0, 0)),
		fi("s2", 6000, 30000, tb(9, "Switch", "priest", "healer", 0, 3000, 0, 10, 0)),
	)
	split := Build("Tacmdbilo", 0, 1<<62, fights, nil, Floors{}, defLim())
	rows := 0
	for _, ok := range split.OK {
		if ok.Name == "Switch" {
			rows++
		}
	}
	if rows != 2 {
		t.Fatalf("Switch rows = %d, want 2 (guid 9 seen as dps and as healer)", rows)
	}
}

func TestRuleWarningsCountedPerFight(t *testing.T) {
	var fights []Input
	for i := range 3 {
		f := fi("f"+itoa(i), int64(1000+i*100), 30000,
			tb(1, "Warned", "rogue", "dps", 3000, 0, 10, 0, 0),
			tb(2, "TankA", "warrior", "tank", 0, 0, 20, 0, 0),
			tb(3, "TankB", "warrior", "tank", 0, 0, 20, 0, 0),
			tb(4, "Chatty", "mage", "dps", 3000, 0, 10, 0, 0),
		)
		if i < 2 {
			f.Warnings = append(f.Warnings, combat.Warning{Rule: "dps_no_damage", Level: "red", Bot: "Warned", Role: "dps", Text: "Warned did no damage"})
		}
		f.Warnings = append(f.Warnings, combat.Warning{Rule: "note", Level: "info", Bot: "Chatty", Role: "dps", Text: "Chatty note"})
		fights = append(fights, f)
	}
	r := Build("Tacmdbilo", 0, 1<<62, fights, nil, Floors{}, defLim())
	row := findBot(t, r, "Warned")
	if row == nil {
		t.Fatalf("Warned not listed")
	}
	reason := findReason(row, "rule:dps_no_damage")
	if reason == nil || reason.Fights != 2 || reason.Level != "red" || row.Flagged != 2 {
		t.Fatalf("reason = %+v, flagged = %d; want 2 fights, red, flagged 2", reason, row.Flagged)
	}
	if row.PeerFights >= defLim().MinFights {
		t.Errorf("peer_fights = %d, want below MinFights", row.PeerFights)
	}
	if findBot(t, r, "Chatty") != nil {
		t.Errorf("Chatty listed on an info warning alone")
	}
}

func TestSkillRateFromRules(t *testing.T) {
	rules := rulesOf(t)
	var bots []combat.FightBot
	w1 := tb(1, "W1", "warrior", "tank", 0, 0, 20, 0, 0)
	w1.Spells = []combat.SpellCount{{N: "Sunder Armor", C: 20}}
	w2 := tb(2, "W2", "warrior", "tank", 0, 0, 10, 0, 0)
	w2.Spells = []combat.SpellCount{{N: "Sunder Armor", C: 10}}
	d1 := tb(3, "D1", "druid", "tank", 0, 0, 10, 0, 0)
	d1.Spells = []combat.SpellCount{{N: "Maul", C: 6}, {N: "Swipe", C: 4}}
	w3 := tb(4, "W3", "warrior", "tank", 0, 0, 0, 0, 0)
	for i := range 12 {
		w3.Spells = append(w3.Spells, combat.SpellCount{N: "Spell" + itoa(i), C: 1})
	}
	bots = append(bots, w1, w2, d1, w3)
	f := fi("f1", 1000, 60000, bots...)
	// A note on each row under test so an unlisted one still has a row to read.
	f.Warnings = []combat.Warning{
		{Rule: "skill_note", Level: "yellow", Bot: "W1", Role: "tank", Text: "W1 note"},
		{Rule: "skill_note", Level: "yellow", Bot: "D1", Role: "tank", Text: "D1 note"},
	}
	r := Build("Tacmdbilo", 0, 1<<62, []Input{f}, rules, ParseFloors(rules), defLim())
	w1row := findBot(t, r, "W1")
	if w1row == nil {
		t.Fatalf("W1 not listed; cannot read its skill rate")
	}
	if w1row.You.Skill10 == nil || !roundEq(*w1row.You.Skill10, 3.33) {
		t.Errorf("W1 you.skill10 = %v, want 3.33", w1row.You.Skill10)
	}
	if w1row.Peers.Skill10 == nil || !roundEq(*w1row.Peers.Skill10, 1.67) {
		t.Errorf("W1 peers.skill10 = %v, want W2's 1.67 (W3's list is cut)", w1row.Peers.Skill10)
	}
	d1row := findBot(t, r, "D1")
	if d1row != nil {
		if d1row.Peers.Skill10 != nil {
			t.Errorf("druid peers.skill10 = %v, want null (no same-class peer)", *d1row.Peers.Skill10)
		}
		if d1row.You.Skill10 == nil || !roundEq(*d1row.You.Skill10, 1.67) {
			t.Errorf("druid you.skill10 = %v, want 1.67 from Maul+Swipe", d1row.You.Skill10)
		}
	}
	w3row := findBot(t, r, "W3")
	if w3row != nil && w3row.You.Skill10 != nil {
		t.Errorf("cut list you.skill10 = %v, want null", *w3row.You.Skill10)
	}
}

func TestLowMarkNeedsRoleFloor(t *testing.T) {
	rulesBytes := []byte(`[{"id":"dps_no_damage","level":"red","role":"dps","condition":"no_damage","min_role_median":2000,"min_fight_s":20,"text":"{bot} did no damage"}]`)
	rules, err := combat.LoadRules(rulesBytes)
	if err != nil {
		t.Fatalf("rules: %v", err)
	}
	// The floor is the rule's unit: raw damage in the fight.
	build := func(peerDmg uint64) Report {
		var fights []Input
		for i := range 3 {
			fights = append(fights, fi("f"+itoa(i), int64(1000+i*100), 30000,
				tb(1, "Low", "rogue", "dps", 100, 0, 10, 0, 0),
				tb(2, "B", "mage", "dps", peerDmg, 0, 10, 0, 0),
				tb(3, "C", "hunter", "dps", peerDmg, 0, 10, 0, 0),
				tb(4, "D", "warlock", "dps", peerDmg, 0, 10, 0, 0),
			))
		}
		return Build("Tacmdbilo", 0, 1<<62, fights, rules, ParseFloors(rules), defLim())
	}
	noMark := build(1500)
	row := findBot(t, noMark, "Low")
	if row == nil {
		t.Fatalf("Low not listed by peer:main")
	}
	if row.Flagged != 0 || len(row.FightRefs) != 0 {
		t.Errorf("1500 damage median: flagged = %d, refs = %d, want no low mark", row.Flagged, len(row.FightRefs))
	}
	marked := build(2500)
	row = findBot(t, marked, "Low")
	if row == nil || row.Flagged != 3 {
		t.Fatalf("2500 damage median: row = %+v, want flagged 3", row)
	}
	noField, err := combat.LoadRules([]byte(`[{"id":"dps_no_damage","level":"red","role":"dps","condition":"no_damage","min_fight_s":20,"text":"{bot} did no damage"}]`))
	if err != nil {
		t.Fatalf("rules: %v", err)
	}
	var fights []Input
	for i := range 3 {
		fights = append(fights, fi("f"+itoa(i), int64(1000+i*100), 30000,
			tb(1, "Low", "rogue", "dps", 100, 0, 10, 0, 0),
			tb(2, "B", "mage", "dps", 1500, 0, 10, 0, 0),
			tb(3, "C", "hunter", "dps", 1500, 0, 10, 0, 0),
			tb(4, "D", "warlock", "dps", 1500, 0, 10, 0, 0),
		))
	}
	row = findBot(t, Build("Tacmdbilo", 0, 1<<62, fights, noField, ParseFloors(noField), defLim()), "Low")
	if row == nil || row.Flagged != 3 {
		t.Fatalf("no floor field: row = %+v, want flagged 3", row)
	}
}

func TestRunsSplitOnGapAndSession(t *testing.T) {
	mk := func(key string, start int64, session uint64) Input {
		return Input{Key: key, Session: session, Fight: combat.Fight{ID: key, Start: start, DurMS: 1000}}
	}
	fights := []Input{mk("a", 1000, 1), mk("b", 1601, 1), mk("c", 2502, 1), mk("d", 3403, 1), mk("e", 4305, 1)}
	runs := Runs(fights, 900)
	if len(runs) != 2 {
		t.Fatalf("runs = %+v, want 2 (600 and two 900 s gaps stay, 901 s splits)", runs)
	}
	if runs[0].Fights != 4 || runs[0].Since != 1000 || runs[0].Until != 3405 {
		t.Errorf("run 1 = %+v, want 4 fights 1000..3405", runs[0])
	}
	if runs[1].Fights != 1 || runs[1].Since != 4305 {
		t.Errorf("run 2 = %+v, want the 4305 fight alone", runs[1])
	}
	// A session change splits even inside the gap.
	sessionChange := Runs([]Input{mk("a", 1000, 1), mk("b", 1601, 2)}, 900)
	if len(sessionChange) != 2 {
		t.Fatalf("session change runs = %+v, want 2", sessionChange)
	}
}

func TestWindowHalfOpen(t *testing.T) {
	mk := func(key string, start int64) Input {
		return Input{Key: key, Day: "2026-11-01", Fight: combat.Fight{ID: key, Start: start, DurMS: 30000,
			Bots: []combat.FightBot{tb(1, "Solo", "rogue", "dps", 3000, 0, 10, 0, 0)}}}
	}
	r := Build("Tacmdbilo", 100, 200, []Input{mk("a", 99), mk("b", 100), mk("c", 199), mk("d", 200)}, nil, Floors{}, defLim())
	if r.Fights != 2 {
		t.Fatalf("fights = %d, want 2 (100 and 199 in, 99 and 200 out)", r.Fights)
	}
	if len(r.OK) != 1 || r.OK[0].Judged != 2 {
		t.Errorf("ok = %+v, want Solo judged in the two in-window fights", r.OK)
	}
}

func TestOrderAndRefsCap(t *testing.T) {
	var fights []Input
	for i := range 25 {
		f := fi("f"+itoa(i), int64(1000+i*1000), 30000,
			tb(1, "Many", "rogue", "dps", 3000, 0, 10, 0, 0),
			tb(2, "TankA", "warrior", "tank", 0, 0, 20, 0, 0),
			tb(3, "Y5", "mage", "dps", 3000, 0, 10, 0, 0),
			tb(4, "Y5b", "hunter", "dps", 3000, 0, 10, 0, 0),
			tb(5, "Y3", "warlock", "dps", 3000, 0, 10, 0, 0),
		)
		f.Warnings = []combat.Warning{{Rule: "note", Level: "yellow", Bot: "Many", Role: "dps", Text: "Many note"}}
		switch {
		case i == 0:
			f.Warnings = append(f.Warnings, combat.Warning{Rule: "bad", Level: "red", Bot: "Many", Role: "dps", Text: "Many bad"})
		case i >= 1 && i <= 5:
			f.Warnings = append(f.Warnings, combat.Warning{Rule: "yellow5", Level: "yellow", Bot: "Y5", Role: "dps", Text: "Y5 note"})
		case i >= 6 && i <= 10:
			f.Warnings = append(f.Warnings, combat.Warning{Rule: "yellow5", Level: "yellow", Bot: "Y5b", Role: "dps", Text: "Y5b note"})
		case i >= 11 && i <= 13:
			f.Warnings = append(f.Warnings, combat.Warning{Rule: "yellow3", Level: "yellow", Bot: "Y3", Role: "dps", Text: "Y3 note"})
		}
		if i == 0 {
			for _, name := range []string{"Y5", "Y5b", "Y3"} {
				f.Warnings = append(f.Warnings, combat.Warning{Rule: "note", Level: "yellow", Bot: name, Role: "dps", Text: name + " note"})
			}
		}
		fights = append(fights, f)
	}
	r := Build("Tacmdbilo", 0, 1<<62, fights, nil, Floors{}, defLim())
	if len(r.Bots) != 4 {
		t.Fatalf("bots = %+v, want Many, Y5, Y5b, Y3", r.Bots)
	}
	want := []string{"Many", "Y5", "Y5b", "Y3"}
	for i, name := range want {
		if r.Bots[i].Name != name {
			t.Fatalf("order = %v, want %v", []string{r.Bots[0].Name, r.Bots[1].Name, r.Bots[2].Name, r.Bots[3].Name}, want)
		}
	}
	many := findBot(t, r, "Many")
	if many.Level != "red" || many.Flagged != 25 {
		t.Errorf("Many level = %q flagged = %d, want red and 25", many.Level, many.Flagged)
	}
	if len(many.FightRefs) != 20 {
		t.Fatalf("refs = %d, want 20", len(many.FightRefs))
	}
	if many.FightRefs[0].Key != "f24" || many.FightRefs[19].Key != "f5" {
		t.Errorf("refs = %q..%q, want f24..f5 (newest first)", many.FightRefs[0].Key, many.FightRefs[19].Key)
	}
}

func TestCauses(t *testing.T) {
	// 1: did nothing in 2 judged fights, listed by the lower dps.
	var fights []Input
	for i := range 4 {
		dmg := uint64(3000)
		if i < 2 {
			dmg = 0
		}
		f := fi("f"+itoa(i), int64(1000+i*100), 30000,
			tb(1, "Idle", "rogue", "dps", dmg, 0, 0, 0, 0),
			tb(2, "B", "mage", "dps", 3000, 0, 10, 0, 0),
			tb(3, "C", "hunter", "dps", 3000, 0, 10, 0, 0),
		)
		f.Warnings = []combat.Warning{{Rule: "note", Level: "yellow", Bot: "Idle", Role: "dps", Text: "Idle note"}}
		fights = append(fights, f)
	}
	r := Build("Tacmdbilo", 0, 1<<62, fights, nil, Floors{}, defLim())
	row := findBot(t, r, "Idle")
	if row == nil || len(row.Causes) == 0 || !strings.Contains(row.Causes[0], "in 2 fights") {
		t.Fatalf("causes = %+v, want the did-nothing line with 2 fights", row)
	}

	// 2: flagged twice, died before half in one.
	fights = nil
	for i := range 2 {
		f := fi("e"+itoa(i), int64(1000+i*100), 60000,
			tb(1, "Early", "rogue", "dps", 6000, 0, 10, 0, 0),
			tb(2, "B", "mage", "dps", 6000, 0, 10, 0, 0),
			tb(3, "C", "hunter", "dps", 6000, 0, 10, 0, 0),
		)
		f.Warnings = []combat.Warning{{Rule: "note", Level: "yellow", Bot: "Early", Role: "dps", Text: "Early note"}}
		if i == 0 {
			f.Fight.Bots[0].Deaths = 1
			f.Fight.Deaths = []combat.FightDeath{{Name: "Early", TMs: 10000}}
		}
		fights = append(fights, f)
	}
	row = findBot(t, Build("Tacmdbilo", 0, 1<<62, fights, nil, Floors{}, defLim()), "Early")
	if row == nil || len(row.Causes) == 0 || !strings.Contains(row.Causes[0], "1 of 2 flagged fights") {
		t.Fatalf("causes = %+v, want the died-before-half line at exactly 0.5", row)
	}

	// 3: low activity against the peers' casts per second.
	fights = nil
	for i := range 4 {
		fights = append(fights, fi("q"+itoa(i), int64(1000+i*100), 30000,
			tb(1, "Quiet", "rogue", "dps", 3000, 0, 3, 0, 0),
			tb(2, "B", "mage", "dps", 3000, 0, 10, 0, 0),
			tb(3, "C", "hunter", "dps", 3000, 0, 10, 0, 0),
		))
	}
	row = findBot(t, Build("Tacmdbilo", 0, 1<<62, fights, nil, Floors{}, defLim()), "Quiet")
	if row == nil || len(row.Causes) == 0 || !strings.Contains(row.Causes[0], "cast 0.1/s against a role median of 0.33/s") {
		t.Fatalf("causes = %+v, want the low-activity line", row)
	}
}

func TestSpellListCapPinned(t *testing.T) {
	b, err := os.ReadFile("../../../../runtime/CombatMonitorPolicy.h")
	if err != nil {
		t.Skip("runtime/CombatMonitorPolicy.h is not in this checkout")
	}
	if !strings.Contains(string(b), "kMaxSpellsPerBot = 12") {
		t.Errorf("SpellListCap = %d, but runtime/CombatMonitorPolicy.h no longer says kMaxSpellsPerBot = 12", defLim().SpellListCap)
	}
	if got := defLim().SpellListCap; got != 12 {
		t.Errorf("SpellListCap = %d, want 12", got)
	}
}

type fixtureFight struct {
	Fight    combat.Fight     `json:"fight"`
	Warnings []combat.Warning `json:"warnings"`
}

// warnSet is bot -> rule -> fights, red and yellow warnings only: exactly
// what the view's rule reasons must say.
func warnSet(in []Input, judge bool, rules []combat.Rule) map[string]map[string]int {
	out := map[string]map[string]int{}
	for _, f := range in {
		ws := f.Warnings
		if judge {
			ws = combat.Judge(f.Fight, rules)
		}
		for _, w := range ws {
			if w.Level != "red" && w.Level != "yellow" {
				continue
			}
			if out[w.Bot] == nil {
				out[w.Bot] = map[string]int{}
			}
			out[w.Bot][w.Rule]++
		}
	}
	return out
}

func ruleReasonSet(r Report) map[string]map[string]int {
	out := map[string]map[string]int{}
	for _, row := range r.Bots {
		for _, reason := range row.Reasons {
			if strings.HasPrefix(reason.ID, "rule:") {
				if out[row.Name] == nil {
					out[row.Name] = map[string]int{}
				}
				out[row.Name][strings.TrimPrefix(reason.ID, "rule:")] = reason.Fights
			}
		}
	}
	return out
}

func sameSet(a, b map[string]map[string]int) bool {
	if len(a) != len(b) {
		return false
	}
	for bot, rules := range a {
		if len(rules) != len(b[bot]) {
			return false
		}
		for rule, n := range rules {
			if b[bot][rule] != n {
				return false
			}
		}
	}
	return true
}

func loadFixture(t *testing.T, path string) []Input {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("fixture: %v", err)
	}
	var raw []fixtureFight
	if err := json.Unmarshal(b, &raw); err != nil {
		t.Fatalf("fixture: %v", err)
	}
	out := make([]Input, 0, len(raw))
	for _, f := range raw {
		out = append(out, Input{
			Key: f.Fight.ID, Day: time.Unix(f.Fight.Start, 0).UTC().Format("2006-01-02"), Fight: f.Fight,
		})
	}
	return out
}

func TestZG24Sample(t *testing.T) {
	rules := rulesOf(t)
	saved := loadFixture(t, "testdata/zg24-sample.json")
	for i := range saved {
		saved[i].Warnings = combat.Judge(saved[i].Fight, rules)
	}
	r := Build("Tacmdbilo", 0, 1<<62, saved, rules, ParseFloors(rules), defLim())
	want := warnSet(saved, true, rules)
	got := ruleReasonSet(r)
	if !sameSet(want, got) {
		t.Fatalf("rule reasons = %v, want %v", got, want)
	}
	if len(want) == 0 {
		t.Fatalf("the sample judged no rule warnings at all")
	}

	// The saved warnings were made with the rules table of the walk day. Once
	// a rules change lands the two differ by design; say so and keep the
	// re-judged comparison above.
	var raw []fixtureFight
	b, err := os.ReadFile("testdata/zg24-sample.json")
	if err != nil {
		t.Fatalf("fixture: %v", err)
	}
	if err := json.Unmarshal(b, &raw); err != nil {
		t.Fatalf("fixture: %v", err)
	}
	var savedIn []Input
	for _, f := range raw {
		savedIn = append(savedIn, Input{Key: f.Fight.ID, Fight: f.Fight, Warnings: f.Warnings})
	}
	savedSet := warnSet(savedIn, false, nil)
	if !sameSet(want, savedSet) {
		t.Logf("the fixture's saved warnings were judged with an older rules table; comparing against combat.Judge only")
	} else if len(savedSet) == 0 {
		t.Errorf("the fixture carries no red/yellow saved warnings")
	}
}

// TestWalkDir is the offline walk match: it reads a walk's saved fights (the
// directory the lead names), keeps the requested window, and checks that the
// view's rule reasons equal the warnings combat.Judge makes from those same
// fights. Opt-in, because it needs the walk files.
func TestWalkDir(t *testing.T) {
	dir := os.Getenv("UNDERPERF_WALK_DIR")
	if dir == "" {
		t.Skip("set UNDERPERF_WALK_DIR, UNDERPERF_SINCE and UNDERPERF_UNTIL to run the walk match")
	}
	since, err := strconv.ParseInt(os.Getenv("UNDERPERF_SINCE"), 10, 64)
	if err != nil {
		t.Fatalf("UNDERPERF_SINCE: %v", err)
	}
	until, err := strconv.ParseInt(os.Getenv("UNDERPERF_UNTIL"), 10, 64)
	if err != nil {
		t.Fatalf("UNDERPERF_UNTIL: %v", err)
	}
	rules := rulesOf(t)
	files, err := filepath.Glob(filepath.Join(dir, "combat-fight-*.json"))
	if err != nil {
		t.Fatalf("walk dir: %v", err)
	}
	var fights []Input
	for _, path := range files {
		b, err := os.ReadFile(path)
		if err != nil {
			t.Fatalf("%s: %v", path, err)
		}
		var ff fixtureFight
		if err := json.Unmarshal(b, &ff); err != nil {
			t.Fatalf("%s: %v", path, err)
		}
		if ff.Fight.Start < since || ff.Fight.Start >= until {
			continue
		}
		fights = append(fights, Input{
			Key: ff.Fight.ID, Day: time.Unix(ff.Fight.Start, 0).UTC().Format("2006-01-02"),
			Fight: ff.Fight, Warnings: combat.Judge(ff.Fight, rules),
		})
	}
	r := Build("Tacmdbilo", since, until, fights, rules, ParseFloors(rules), defLim())
	t.Logf("walk %s: %d fights, %d judged, %d runs", dir, r.Fights, r.Judged, len(r.Runs))
	for _, row := range r.Bots {
		var reasons []string
		for _, reason := range row.Reasons {
			reasons = append(reasons, reason.ID+" x"+itoa(reason.Fights))
		}
		main := ""
		if row.Ratio.Main != nil {
			main = fmt.Sprintf("%s=%v", row.Main, *row.Ratio.Main)
		}
		t.Logf("%-14s %-6s %-28s %s", row.Name, row.Level, strings.Join(reasons, ", "), main)
	}
	want := warnSet(fights, false, nil) // Inputs carry Judge's warnings
	if got := ruleReasonSet(r); !sameSet(want, got) {
		t.Fatalf("rule reasons = %v, want the warnings over the same fights %v", got, want)
	}
}
