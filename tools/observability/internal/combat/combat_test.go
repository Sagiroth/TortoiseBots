package combat

import (
	"encoding/json"
	"testing"
)

// A fixture fight: a 30 s Molten Core pull. Tanky (warrior tank, no Sunder)
// fires tank_no_sunder; Pawswipe (druid tank, casts Maul) fires nothing;
// Healy (priest, 0 heals) fires healer_no_heals; Pew (mage, 0 damage) fires
// dps_no_damage.
func fixtureFight() Fight {
	return Fight{
		ID: "55-1", Commander: "Gbmage", CommanderGUID: 9, Group: 55, Map: 409,
		Plan: "molten_core", Leg: "l1", Step: 3, Kind: "boss",
		Start: 1700023000, DurMS: 30000, Outcome: "kill",
		Boss: 11502, BossName: "Magmadar", Planned: []uint32{11502},
		Engaged: []EngagedEntry{{E: 11502, N: "Magmadar", C: 1}, {E: 11673, N: "Core Hound", C: 2}},
		Bots: []FightBot{
			{Name: "Tanky", GUID: 1, Class: "warrior", Role: "tank", Sub: 0, Dmg: 1200, Heal: 0, Over: 0, Taken: 3000, Casts: 6, Deaths: 0,
				Spells: []SpellCount{{N: "Heroic Strike", C: 6}}},
			{Name: "Pawswipe", GUID: 4, Class: "druid", Role: "tank", Sub: 1, Dmg: 900, Taken: 2500, Casts: 3,
				Spells: []SpellCount{{N: "Maul", C: 3}}},
			{Name: "Healy", GUID: 2, Class: "priest", Role: "healer", Sub: 0, Dmg: 0, Heal: 0, Casts: 0},
			{Name: "Pew", GUID: 3, Class: "mage", Role: "dps", Sub: 0, Dmg: 0, Taken: 400, Casts: 5,
				Spells: []SpellCount{{N: "Fireball", C: 5}}},
		},
		Debuffs: FightDebuffs{SampledS: 10, UpS: map[string]uint64{"sunder5": 6, "faerie_fire": 10}},
		Deaths:  []FightDeath{},
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
	f.Bots[3].Deaths = 1
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
	raw := `{"fight":{"id":"7-2","commander":"Cmdr","commander_guid":3,"group":7,"map":409,"plan":"mc","leg":"l1","step":1,"kind":"boss","start":9,"dur_ms":25000,"outcome":"end","boss":1,"boss_name":"B","planned":[1],"engaged":[{"e":1,"n":"B","c":1}],"bots":[{"name":"T","guid":1,"class":"warrior","role":"tank","sub":-1,"dmg":5,"heal":0,"over":0,"taken":1,"casts":2,"heal_casts":0,"deaths":0,"spells":[{"n":"Sunder Armor","c":2}]}],"debuffs":{"sampled_s":3,"up_s":{"sunder":3}},"deaths":[{"name":"H","t_ms":9000,"x":1.5,"y":2.5,"z":3.5,"killer":"B","hits":[{"a":"B","d":400,"t_ms":8000}]}]}}`
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
}
