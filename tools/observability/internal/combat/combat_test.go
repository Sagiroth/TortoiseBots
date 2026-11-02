package combat

import (
	"encoding/json"
	"os"
	"regexp"
	"strings"
	"testing"
)

// A fixture fight: a 60 s Molten Core pull. Tanky (warrior tank, no Sunder)
// fires tank_no_sunder; Pawswipe (druid tank, casts Maul) fires nothing;
// Healy (priest, 0 heals, peer Mendy heals 5000) fires healer_no_heals; Pew
// (mage, 0 damage, peer Boom deals 6000) fires dps_no_damage.
func fixtureFight() Fight {
	return Fight{
		ID: "55-1", Commander: "Gbmage", CommanderGUID: 9, Group: 55, Map: 409,
		Plan: "molten_core", Leg: "l1", Step: 3, Kind: "boss",
		Start: 1700023000, DurMS: 60000, Outcome: "kill",
		Boss: 11502, BossName: "Magmadar", Planned: []uint32{11502},
		Engaged: []EngagedEntry{{E: 11502, N: "Magmadar", C: 1}, {E: 11673, N: "Core Hound", C: 2}},
		Bots: []FightBot{
			{Name: "Tanky", GUID: 1, Class: "warrior", Role: "tank", Sub: 0, Dmg: 1200, Heal: 0, Over: 0, Taken: 3000, Casts: 6, Deaths: 0,
				Spells: []SpellCount{{N: "Heroic Strike", C: 6}}},
			{Name: "Pawswipe", GUID: 4, Class: "druid", Role: "tank", Sub: 1, Dmg: 900, Taken: 2500, Casts: 3,
				Spells: []SpellCount{{N: "Maul", C: 3}}},
			{Name: "Healy", GUID: 2, Class: "priest", Role: "healer", Sub: 0, Dmg: 0, Heal: 0, Casts: 0},
			{Name: "Mendy", GUID: 5, Class: "priest", Role: "healer", Sub: 1, Dmg: 0, Heal: 5000, HealCasts: 8, Casts: 8,
				Spells: []SpellCount{{N: "Heal", C: 8}}},
			{Name: "Pew", GUID: 3, Class: "mage", Role: "dps", Sub: 0, Dmg: 0, Taken: 400, Casts: 5,
				Spells: []SpellCount{{N: "Fireball", C: 5}}},
			{Name: "Boom", GUID: 6, Class: "mage", Role: "dps", Sub: 1, Dmg: 6000, Taken: 100, Casts: 12,
				Spells: []SpellCount{{N: "Fireball", C: 12}}},
		},
		Deaths: []FightDeath{},
	}
}

func TestJudge(t *testing.T) {
	rules, err := LoadRules(DefaultRules())
	if err != nil {
		t.Fatal(err)
	}
	w := Judge(fixtureFight(), rules)
	byRule := map[string]string{}
	for _, x := range w {
		byRule[x.Rule] = x.Bot
	}
	if byRule["tank_no_sunder"] != "Tanky" {
		t.Fatalf("tank_no_sunder: %v", w)
	}
	if _, ok := byRule["tank_no_maul_swipe"]; ok {
		t.Fatalf("druid tank cast Maul, must warn nothing: %v", w)
	}
	if byRule["healer_no_heals"] != "Healy" {
		t.Fatalf("healer_no_heals: %v", w)
	}
	if byRule["dps_no_damage"] != "Pew" {
		t.Fatalf("dps_no_damage: %v", w)
	}
	for _, x := range w {
		if x.Level != "red" || x.Bot == "" || x.Text == "" {
			t.Fatalf("warning fields: %+v", x)
		}
		if x.Text != "" && len(x.Text) > 0 && x.Text[0] == '{' {
			t.Fatalf("bot name not substituted: %q", x.Text)
		}
	}
}

// A 15 s fight is below every rule's min_fight_s: no warnings.
func TestJudgeShortFightWarnsNothing(t *testing.T) {
	rules, err := LoadRules(DefaultRules())
	if err != nil {
		t.Fatal(err)
	}
	f := fixtureFight()
	f.DurMS = 15000
	if w := Judge(f, rules); len(w) != 0 {
		t.Fatalf("short fight warned: %v", w)
	}
}

// A dps that died at 2 s never got to act: not judged.
func TestJudgeSkipsDeadWholeFight(t *testing.T) {
	rules, err := LoadRules(DefaultRules())
	if err != nil {
		t.Fatal(err)
	}
	f := fixtureFight()
	f.Bots[4].Deaths = 1
	f.Deaths = []FightDeath{{Name: "Pew", TMs: 2000, X: 1, Y: 2, Z: 3, Killer: "Magmadar"}}
	w := Judge(f, rules)
	for _, x := range w {
		if x.Bot == "Pew" {
			t.Fatalf("dead-whole-fight bot judged: %v", w)
		}
	}
}

// A healer that cast heals but healed nothing (everyone full) is working:
// only a healer with no healing is flagged.
func TestJudgeHealerWithCastsButNoHealing(t *testing.T) {
	rules, err := LoadRules(DefaultRules())
	if err != nil {
		t.Fatal(err)
	}
	f := fixtureFight()
	f.Bots[2].Casts = 4
	f.Bots[2].HealCasts = 4
	f.Bots[2].Spells = []SpellCount{{N: "Heal", C: 4}}
	f.Bots[2].Heal = 0
	w := Judge(f, rules)
	for _, x := range w {
		if x.Rule == "healer_no_heals" && x.Bot == "Healy" {
			t.Fatalf("healer that cast heals flagged: %v", w)
		}
	}
}

func TestLoadRulesUnknownCondition(t *testing.T) {
	bad := `[{"id":"x","level":"red","role":"dps","min_fight_s":0,"condition":"bogus_thing:Foo","text":"{bot}"}]`
	if _, err := LoadRules([]byte(bad)); err == nil {
		t.Fatal("unknown condition loaded without error")
	}
}

func TestStoreDedupe(t *testing.T) {
	s := NewStore(nil)
	f := fixtureFight()
	if !s.Add(9, f) {
		t.Fatal("first take refused")
	}
	if s.Add(9, f) {
		t.Fatal("second take kept: dedupe by (session, id) broken")
	}
	if got := s.List("Gbmage"); len(got) != 1 || got[0].ID != "55-1" || got[0].Deaths != 0 || got[0].Warnings != 0 {
		t.Fatalf("list: %+v", got)
	}
	if got := s.List("Gbmage"); len(got) != 1 || got[0].Session != 9 || got[0].Key == "" {
		t.Fatalf("list row misses key/session: %+v", got)
	}
	if _, _, ok := s.Get("Gbmage", 9, "55-1"); !ok {
		t.Fatal("Get missed the stored fight")
	}
	if _, _, ok := s.GetKey(s.List("Gbmage")[0].Key); !ok {
		t.Fatal("GetKey missed the stored fight")
	}
	if _, _, ok := s.Get("Gbmage", 9, "55-999"); ok {
		t.Fatal("Get found a fight that was never stored")
	}
	if _, _, ok := s.Get("Other", 9, "55-1"); ok {
		t.Fatal("Get found another commander's fight")
	}
}

// The same fight id for two commanders and two sessions: four distinct
// fights, each reachable only by its own triple.
func TestStoreCollision(t *testing.T) {
	s := NewStore(nil)
	for _, tc := range []struct {
		commander string
		session   uint64
		boss      string
	}{
		{"Gbmage", 9, "Magmadar"},
		{"Gbmage", 10, "Geddon"},
		{"Other", 9, "Garr"},
		{"Other", 10, "Shazzrah"},
	} {
		f := fixtureFight()
		f.Commander = tc.commander
		f.BossName = tc.boss
		if !s.Add(tc.session, f) {
			t.Fatalf("take refused for %v", tc)
		}
	}
	if got := s.List("Gbmage"); len(got) != 2 {
		t.Fatalf("Gbmage keeps %d fights, want 2", len(got))
	}
	if got := s.List("Other"); len(got) != 2 {
		t.Fatalf("Other keeps %d fights, want 2", len(got))
	}
	for _, tc := range []struct {
		commander string
		session   uint64
		boss      string
	}{
		{"Gbmage", 9, "Magmadar"},
		{"Gbmage", 10, "Geddon"},
		{"Other", 9, "Garr"},
		{"Other", 10, "Shazzrah"},
	} {
		f, _, ok := s.Get(tc.commander, tc.session, "55-1")
		if !ok || f.BossName != tc.boss {
			t.Fatalf("Get(%q, %d): %+v ok=%v, want boss %q", tc.commander, tc.session, f, ok, tc.boss)
		}
	}
}

// 51 fights: the oldest is dropped, 50 kept.
func TestStoreCap(t *testing.T) {
	s := NewStore(nil)
	for i := range 51 {
		f := fixtureFight()
		f.ID = string(rune('a'+i/26)) + string(rune('0'+i%26)) + "-cap"
		f.Commander = "Cap"
		if !s.Add(9, f) {
			t.Fatalf("fight %d refused", i)
		}
	}
	if got := s.List("Cap"); len(got) != 50 {
		t.Fatalf("kept %d fights, want 50", len(got))
	}
}

// The store reads the exact bytes the module writes (A8a BuildFightJson):
// spell counts as [{"n","c"}], death hits as {"a","d","t_ms"}.
func TestStoreReadsModuleBytes(t *testing.T) {
	raw := `{"fight":{"id":"7-2","commander":"Cmdr","commander_guid":3,"group":7,"map":409,"plan":"mc","leg":"l1","step":1,"kind":"boss","start":9,"dur_ms":25000,"outcome":"end","boss":1,"boss_name":"B","planned":[1],"engaged":[{"e":1,"n":"B","c":1}],"bots":[{"name":"T","guid":1,"class":"warrior","role":"tank","sub":-1,"dmg":5,"heal":0,"over":0,"taken":1,"casts":2,"heal_casts":0,"deaths":0,"spells":[{"n":"Sunder Armor","c":2}],"tail_spells":[{"n":"Drink","c":1}]}],"debuffs":{"sampled_s":3,"up_s":{"sunder":3}},"deaths":[{"name":"H","t_ms":9000,"x":1.5,"y":2.5,"z":3.5,"killer":"B","hits":[{"a":"B","d":400,"t_ms":8000}]}]}}`
	var dg struct {
		Fight Fight `json:"fight"`
	}
	if err := json.Unmarshal([]byte(raw), &dg); err != nil {
		t.Fatal(err)
	}
	f := dg.Fight
	if f.ID != "7-2" || len(f.Bots) != 1 || f.Bots[0].Spells[0].N != "Sunder Armor" || f.Bots[0].Spells[0].C != 2 {
		t.Fatalf("fight: %+v", f)
	}
	if len(f.Deaths) != 1 || f.Deaths[0].Hits[0].A != "B" || f.Deaths[0].Hits[0].D != 400 {
		t.Fatalf("deaths: %+v", f.Deaths)
	}
	if f.Debuffs.UpS["sunder"] != 3 || f.Debuffs.SampledS != 3 {
		t.Fatalf("debuffs: %+v", f.Debuffs)
	}
	if len(f.Bots[0].TailSpells) != 1 || f.Bots[0].TailSpells[0].N != "Drink" || f.Bots[0].TailSpells[0].C != 1 {
		t.Fatalf("tail spells: %+v", f.Bots[0].TailSpells)
	}
}
func TestStoreReadsDegradedMarkers(t *testing.T) {
	newPayload := `{"fight":{"id":"7-3","commander":"Cmdr","commander_guid":3,"group":7,"map":409,"plan":"mc","leg":"l1","step":1,"kind":"boss","start":9,"dur_ms":25000,"outcome":"end","boss":1,"boss_name":"B","spells_cut":3,"planned":[1],"engaged":[],"dropped":0,"bots":[{"name":"T","guid":1,"class":"warrior","role":"tank","sub":-1,"dmg":5,"heal":0,"over":0,"taken":1,"casts":2,"heal_casts":0,"deaths":0,"spells":[{"n":"Sunder Armor","c":2}],"tail_spells":[]}],"debuffs":{"sampled_s":3,"up_s":{}},"deaths":[],"truncated":true}}`
	var dg struct {
		Fight Fight `json:"fight"`
	}
	if err := json.Unmarshal([]byte(newPayload), &dg); err != nil {
		t.Fatal(err)
	}
	if dg.Fight.SpellsCut != 3 || !dg.Fight.Truncated {
		t.Fatalf("new payload markers: spells_cut=%d truncated=%v", dg.Fight.SpellsCut, dg.Fight.Truncated)
	}
	oldPayload := `{"fight":{"id":"7-2","commander":"Cmdr","commander_guid":3,"group":7,"map":409,"plan":"mc","leg":"l1","step":1,"kind":"boss","start":9,"dur_ms":25000,"outcome":"end","boss":1,"boss_name":"B","planned":[1],"engaged":[],"bots":[{"name":"T","guid":1,"class":"warrior","role":"tank","sub":-1,"dmg":5,"heal":0,"over":0,"taken":1,"casts":2,"heal_casts":0,"deaths":0,"spells":[{"n":"Sunder Armor","c":2}],"tail_spells":[]}],"debuffs":{"sampled_s":3,"up_s":{}},"deaths":[]}}`
	var old struct {
		Fight Fight `json:"fight"`
	}
	if err := json.Unmarshal([]byte(oldPayload), &old); err != nil {
		t.Fatal(err)
	}
	if old.Fight.SpellsCut != 0 || old.Fight.Truncated {
		t.Fatalf("old payload without markers: spells_cut=%d truncated=%v", old.Fight.SpellsCut, old.Fight.Truncated)
	}
}

// A full build lists every AlwaysListed spell first, so a full 12-spell row
// without Sunder proves it went uncast: the rule still judges.
func TestRuleSkipsTruncatedSpellList(t *testing.T) {
	mkTank := func(names ...string) Fight {
		spells := make([]SpellCount, 0, len(names))
		for _, n := range names {
			spells = append(spells, SpellCount{N: n, C: 1})
		}
		return Fight{ID: "t1", Commander: "C", Start: 1, DurMS: 60000,
			Bots: []FightBot{{Name: "Tanky", GUID: 1, Class: "warrior", Role: "tank", Dmg: 100, Spells: spells}}}
	}
	sunder := `[{"id":"tank_no_sunder","level":"red","role":"tank","class":"warrior","min_fight_s":20,"condition":"spell_count_zero:Sunder Armor","text":"{bot}"}]`
	rules, err := LoadRules([]byte(sunder))
	if err != nil {
		t.Fatal(err)
	}
	full := []string{"Heroic Strike", "Revenge", "Shield Block", "Taunt", "Bloodrage", "Slam",
		"Cleave", "Overpower", "Rend", "Thunder Clap", "Demoralizing Shout", "Battle Shout"}
	if w := Judge(mkTank(full...), rules); len(w) != 1 {
		t.Fatalf("12 distinct listed spells, no Sunder: want 1 warning, got %v", w)
	}
	short := []string{"Heroic Strike", "Revenge"}
	if w := Judge(mkTank(short...), rules); len(w) != 1 {
		t.Fatalf("2 listed spells, no Sunder: want 1 warning, got %v", w)
	}
}

// A 3-spell degraded payload cuts the rest-by-count tail first: a full row
// ending in an always-listed spell may still hide casts past it, but a row
// ending in anything else proves the absence.
func TestRuleSkipsThreeSpellDegraded(t *testing.T) {
	mkTank := func(spells ...SpellCount) Fight {
		return Fight{ID: "t3", Commander: "C", Start: 1, DurMS: 60000, SpellsCut: 3,
			Bots: []FightBot{{Name: "Tanky", GUID: 1, Class: "warrior", Role: "tank", Dmg: 100, Spells: spells}}}
	}
	sunder := `[{"id":"tank_no_sunder","level":"red","role":"tank","class":"warrior","min_fight_s":20,"condition":"spell_count_zero:Sunder Armor","text":"{bot}"}]`
	rules, err := LoadRules([]byte(sunder))
	if err != nil {
		t.Fatal(err)
	}
	hidden := mkTank(SpellCount{N: "Heroic Strike", C: 1}, SpellCount{N: "Revenge", C: 2}, SpellCount{N: "Shield Block", C: 1})
	if w := Judge(hidden, rules); len(w) != 0 {
		t.Fatalf("3-spell row ending in an always-listed spell may hide Sunder: want no warning, got %v", w)
	}
	proven := mkTank(SpellCount{N: "Heroic Strike", C: 1}, SpellCount{N: "Revenge", C: 2}, SpellCount{N: "Bloodrage", C: 1})
	if w := Judge(proven, rules); len(w) != 1 {
		t.Fatalf("3-spell row ending in a non-listed spell proves no Sunder: want 1 warning, got %v", w)
	}
	shortRow := mkTank(SpellCount{N: "Heroic Strike", C: 1}, SpellCount{N: "Bloodrage", C: 1})
	if w := Judge(shortRow, rules); len(w) != 1 {
		t.Fatalf("short 3-rung row proves no Sunder: want 1 warning, got %v", w)
	}
}

// A 0-spell degraded payload lists nothing per bot: every spell-absence
// rule gives no verdict on it.
func TestRuleSkipsZeroSpellDegraded(t *testing.T) {
	mkTank := func() Fight {
		return Fight{ID: "t0", Commander: "C", Start: 1, DurMS: 60000,
			Bots: []FightBot{{Name: "Tanky", GUID: 1, Class: "warrior", Role: "tank", Dmg: 100}}}
	}
	sunder := `[{"id":"tank_no_sunder","level":"red","role":"tank","class":"warrior","min_fight_s":20,"condition":"spell_count_zero:Sunder Armor","text":"{bot}"}]`
	swipe := `[{"id":"tank_no_maul_swipe","level":"red","role":"tank","class":"druid","min_fight_s":20,"condition":"spells_count_zero:Maul,Swipe","text":"{bot}"}]`
	rules, err := LoadRules([]byte(sunder))
	if err != nil {
		t.Fatal(err)
	}
	f := mkTank()
	f.SpellsCut = 0
	f.Truncated = true
	if w := Judge(f, rules); len(w) != 0 {
		t.Fatalf("0-spell degraded payload may hide Sunder: want no warning, got %v", w)
	}
	druid := Fight{ID: "t0", Commander: "C", Start: 1, DurMS: 60000,
		SpellsCut: 0, Truncated: true,
		Bots: []FightBot{{Name: "Pawswipe", GUID: 4, Class: "druid", Role: "tank", Dmg: 100}}}
	druidRules, err := LoadRules([]byte(swipe))
	if err != nil {
		t.Fatal(err)
	}
	if w := Judge(druid, druidRules); len(w) != 0 {
		t.Fatalf("0-spell degraded payload may hide Maul/Swipe: want no warning, got %v", w)
	}
	if w := Judge(mkTank(), rules); len(w) != 1 {
		t.Fatalf("full payload, no Sunder: want 1 warning, got %v", w)
	}
}

// The no_damage rule fires only when the peer dps median clears the floor:
// an idle raid punishes nobody.
func TestNoDamageNeedsPeerMedian(t *testing.T) {
	mkFight := func(peerDmg uint64) Fight {
		return Fight{ID: "d1", Commander: "C", Start: 1, DurMS: 60000,
			Bots: []FightBot{
				{Name: "Pew", GUID: 1, Class: "mage", Role: "dps", Dmg: 0},
				{Name: "Peer", GUID: 2, Class: "mage", Role: "dps", Dmg: peerDmg},
			}}
	}
	idle := `[{"id":"dps_no_damage","level":"red","role":"dps","min_fight_s":0,"condition":"no_damage","text":"{bot}","min_role_median":2000}]`
	rules, err := LoadRules([]byte(idle))
	if err != nil {
		t.Fatal(err)
	}
	if w := Judge(mkFight(1000), rules); len(w) != 0 {
		t.Fatalf("peer median 1000 below the 2000 floor: want no warning, got %v", w)
	}
	if w := Judge(mkFight(2000), rules); len(w) != 1 || w[0].Bot != "Pew" {
		t.Fatalf("peer median 2000 meets the 2000 floor: want Pew warned, got %v", w)
	}
	if w := Judge(mkFight(5000), rules); len(w) != 1 || w[0].Bot != "Pew" {
		t.Fatalf("peer median 5000 clears the 2000 floor: want Pew warned, got %v", w)
	}
}

// The no_healing rule fires only when the peer healer median clears the
// floor: a quiet pull punishes nobody.
func TestNoHealsNeedsPeerMedian(t *testing.T) {
	mkFight := func(peerHeal uint64) Fight {
		return Fight{ID: "h1", Commander: "C", Start: 1, DurMS: 60000,
			Bots: []FightBot{
				{Name: "Healy", GUID: 1, Class: "priest", Role: "healer", Heal: 0},
				{Name: "Peer", GUID: 2, Class: "priest", Role: "healer", Heal: peerHeal},
			}}
	}
	quiet := `[{"id":"healer_no_heals","level":"red","role":"healer","min_fight_s":0,"condition":"no_healing","text":"{bot}","min_role_median":3000}]`
	rules, err := LoadRules([]byte(quiet))
	if err != nil {
		t.Fatal(err)
	}
	if w := Judge(mkFight(500), rules); len(w) != 0 {
		t.Fatalf("peer median 500 below the 3000 floor: want no warning, got %v", w)
	}
	if w := Judge(mkFight(3000), rules); len(w) != 1 || w[0].Bot != "Healy" {
		t.Fatalf("peer median 3000 meets the 3000 floor: want Healy warned, got %v", w)
	}
	if w := Judge(mkFight(4000), rules); len(w) != 1 || w[0].Bot != "Healy" {
		t.Fatalf("peer median 4000 clears the 3000 floor: want Healy warned, got %v", w)
	}
}

// A lone healer (no role peers) has no median to clear: healer_no_heals
// fires only for a long fight where it healed nothing while the group took
// real damage. A lone dps is still never judged.
func TestLoneHealerFallback(t *testing.T) {
	mkHealer := func(durMS uint32, heal, taken uint64) Fight {
		return Fight{ID: "h2", Commander: "C", Start: 1, DurMS: durMS,
			Bots: []FightBot{
				{Name: "Healy", GUID: 1, Class: "priest", Role: "healer", Heal: heal, Taken: 500},
				{Name: "Tanky", GUID: 2, Class: "warrior", Role: "tank", Taken: taken},
			}}
	}
	quiet := `[{"id":"healer_no_heals","level":"red","role":"healer","min_fight_s":0,"condition":"no_healing","text":"{bot}","min_role_median":3000}]`
	rules, err := LoadRules([]byte(quiet))
	if err != nil {
		t.Fatal(err)
	}
	if w := Judge(mkHealer(60000, 0, 4000), rules); len(w) != 1 || w[0].Bot != "Healy" {
		t.Fatalf("lone healer, 60 s, group took 4500: want Healy warned, got %v", w)
	}
	if w := Judge(mkHealer(59000, 0, 4000), rules); len(w) != 0 {
		t.Fatalf("lone healer, 59 s: want no warning, got %v", w)
	}
	if w := Judge(mkHealer(60000, 0, 2000), rules); len(w) != 0 {
		t.Fatalf("lone healer, group took 2500: want no warning, got %v", w)
	}
	if w := Judge(mkHealer(60000, 100, 4000), rules); len(w) != 0 {
		t.Fatalf("lone healer that healed: want no warning, got %v", w)
	}
	earlyDeath := Fight{ID: "h2", Commander: "C", Start: 1, DurMS: 69000,
		Bots: []FightBot{
			{Name: "Healy", GUID: 1, Class: "priest", Role: "healer", Heal: 0, Taken: 500, Deaths: 1},
			{Name: "Tanky", GUID: 2, Class: "warrior", Role: "tank", Taken: 4000},
		},
		Deaths: []FightDeath{{Name: "Healy", TMs: 12000, Killer: "B"}}}
	if w := Judge(earlyDeath, rules); len(w) != 0 {
		t.Fatalf("lone healer dead at 12 s of 69 s: want no warning, got %v", w)
	}
}

func TestLoneDpsNeverJudged(t *testing.T) {
	f := Fight{ID: "d2", Commander: "C", Start: 1, DurMS: 120000,
		Bots: []FightBot{
			{Name: "Pew", GUID: 1, Class: "mage", Role: "dps", Dmg: 0, Taken: 2000},
			{Name: "Tanky", GUID: 2, Class: "warrior", Role: "tank", Dmg: 5000, Taken: 4000},
		}}
	idle := `[{"id":"dps_no_damage","level":"red","role":"dps","min_fight_s":0,"condition":"no_damage","text":"{bot}","min_role_median":2000}]`
	rules, err := LoadRules([]byte(idle))
	if err != nil {
		t.Fatal(err)
	}
	if w := Judge(f, rules); len(w) != 0 {
		t.Fatalf("lone dps, no peers: want no warning, got %v", w)
	}
}

// A bot dead before half the fight is not judged on output: a death early
// is not idleness. A death past half still judges.
func TestDeadInFirstHalfNotJudged(t *testing.T) {
	mkFight := func(deathMS uint32) Fight {
		return Fight{ID: "e1", Commander: "C", Start: 1, DurMS: 69000,
			Bots: []FightBot{
				{Name: "Pew", GUID: 1, Class: "mage", Role: "dps", Dmg: 0, Deaths: 1},
				{Name: "Peer", GUID: 2, Class: "mage", Role: "dps", Dmg: 9000},
			},
			Deaths: []FightDeath{{Name: "Pew", TMs: deathMS, Killer: "B"}}}
	}
	row := `[{"id":"dps_no_damage","level":"red","role":"dps","min_fight_s":0,"condition":"no_damage","text":"{bot}","min_role_median":2000}]`
	rules, err := LoadRules([]byte(row))
	if err != nil {
		t.Fatal(err)
	}
	if w := Judge(mkFight(12000), rules); len(w) != 0 {
		t.Fatalf("death at 12 s of 69 s: want no warning, got %v", w)
	}
	if w := Judge(mkFight(40000), rules); len(w) != 1 || w[0].Bot != "Pew" {
		t.Fatalf("death at 40 s of 69 s: want Pew warned, got %v", w)
	}
}

// A bot with deaths but no death row died at an unknown time: no idle or
// no-output verdict (the last degraded build drops the death rows but keeps
// each bot's deaths count).
func TestDeadUnknownTimeNotJudged(t *testing.T) {
	mkFight := func(deaths uint32, rows []FightDeath) Fight {
		return Fight{ID: "e2", Commander: "C", Start: 1, DurMS: 69000,
			Bots: []FightBot{
				{Name: "Pew", GUID: 1, Class: "mage", Role: "dps", Dmg: 0, Deaths: deaths},
				{Name: "Peer", GUID: 2, Class: "mage", Role: "dps", Dmg: 9000},
			},
			Deaths: rows}
	}
	idle := `[{"id":"dps_no_damage","level":"red","role":"dps","min_fight_s":0,"condition":"no_damage","text":"{bot}","min_role_median":2000}]`
	rules, err := LoadRules([]byte(idle))
	if err != nil {
		t.Fatal(err)
	}
	if w := Judge(mkFight(1, nil), rules); len(w) != 0 {
		t.Fatalf("deaths=1 with no death row: want no warning, got %v", w)
	}
	if w := Judge(mkFight(1, []FightDeath{{Name: "Pew", TMs: 40000, Killer: "B"}}), rules); len(w) != 1 {
		t.Fatalf("death at 40 s of 69 s: want Pew warned, got %v", w)
	}
	if w := Judge(mkFight(0, nil), rules); len(w) != 1 {
		t.Fatalf("alive with no damage: want 1 warning, got %v", w)
	}
}

// The shipped tank_no_sunder row must wait for a 45 s fight, not a 20 s
// trash pull: at 44 s it warns nothing, at 45 s it judges.
func TestSunderRuleNeeds45s(t *testing.T) {
	mkTank := func(dur uint32) Fight {
		return Fight{ID: "s1", Commander: "C", Start: 1, DurMS: dur,
			Bots: []FightBot{{Name: "Tanky", GUID: 1, Class: "warrior", Role: "tank", Dmg: 100,
				Spells: []SpellCount{{N: "Heroic Strike", C: 3}}}}}
	}
	rules, err := LoadRules(DefaultRules())
	if err != nil {
		t.Fatal(err)
	}
	if w := Judge(mkTank(44000), rules); len(w) != 0 {
		t.Fatalf("44 s trash fight: want no warning, got %v", w)
	}
	if w := Judge(mkTank(45000), rules); len(w) != 1 {
		t.Fatalf("45 s fight: want 1 warning, got %v", w)
	}
}

// The 3-spell skip keys on the module's always-listed spell names: the pin
// reads them from runtime/CombatMonitorPolicy.h.
func TestAlwaysListedSpellsPinnedToModule(t *testing.T) {
	raw, err := os.ReadFile("../../../../runtime/CombatMonitorPolicy.h")
	if err != nil {
		t.Skip("module checkout not above the dashboard")
	}
	for name := range alwaysListedSpells {
		if !strings.Contains(string(raw), `"`+name+`"`) {
			t.Fatalf("always-listed spell %q not found in runtime/CombatMonitorPolicy.h", name)
		}
	}
	m := regexp.MustCompile(`spell == "([^"]+)"`).FindAllSubmatch(raw, -1)
	if m == nil {
		t.Fatal("AlwaysListed spells not found in runtime/CombatMonitorPolicy.h")
	}
	for _, one := range m {
		if !alwaysListedSpells[string(one[1])] {
			t.Fatalf("module always-lists %q, Go mirror misses it", one[1])
		}
	}
}
