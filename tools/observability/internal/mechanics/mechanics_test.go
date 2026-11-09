package mechanics

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"regexp"
	"strconv"
	"strings"
	"testing"

	"tortoise-observability/internal/combat"
)

const (
	poison = 24321
	siphon = 24324
	hex    = 17172
)

func raws(rows ...string) []json.RawMessage {
	out := make([]json.RawMessage, len(rows))
	for i, r := range rows {
		out[i] = json.RawMessage(r)
	}
	return out
}

func bot(guid uint32, name, role string, taken uint64) combat.FightBot {
	return combat.FightBot{GUID: guid, Name: name, Role: role, Taken: taken}
}

// hakkar is the dash-2 fixture: A tank, B healer, C and D dps; D dies at 60 s; Blood Siphon at 35 s and 95 s. A has Poisonous Blood
// 10-40 s, B from 30 s to the end, C from the pull to 5 s, D never: 2 of 4 alive at 35 s, 1 of 3 at 95 s.
func hakkar(extraAuras ...string) combat.Fight {
	auras := append([]string{
		fmt.Sprintf("[10000,1,%d,1]", poison), fmt.Sprintf("[40000,1,%d,-1]", poison),
		fmt.Sprintf("[30000,2,%d,1]", poison), fmt.Sprintf("[5000,3,%d,-1]", poison),
	}, extraAuras...)
	return combat.Fight{
		ID: "h", Boss: 14834, BossName: "Hakkar", DurMS: 120000, Outcome: "kill",
		Bots:   []combat.FightBot{bot(1, "A", "tank", 900), bot(2, "B", "healer", 0), bot(3, "C", "dps", 0), bot(4, "D", "dps", 0)},
		Deaths: []combat.FightDeath{{Name: "D", TMs: 60000, Killer: "Hakkar"}},
		Log: raws(
			fmt.Sprintf(`{"k":"cast","t":35000,"s":"Blood Siphon","sid":%d}`, siphon),
			fmt.Sprintf(`{"k":"cast","t":95000,"s":"Blood Siphon","sid":%d}`, siphon),
			`{"k":"cast","t":50000,"s":"Cause Insanity","sid":24327}`,
			`{"k":"death","t":60000,"n":"D","by":"Hakkar"}`),
		Auras: raws(auras...),
	}
}

func row(t *testing.T, rows []Row, boss uint32) Row {
	t.Helper()
	for _, r := range rows {
		if r.Boss == boss {
			return r
		}
	}
	t.Fatalf("no row for boss %d in %+v", boss, rows)
	return Row{}
}

func near(t *testing.T, what string, got *float64, want float64) {
	t.Helper()
	if got == nil || math.Abs(*got-want) > 1e-9 {
		t.Fatalf("%s = %v, want %v", what, got, want)
	}
}

// Failing case of this branch: the package does not exist on dash/wipe-recap.
func TestFoldHakkarDosed(t *testing.T) {
	one := hakkar() // 2 of 4 at 35 s, 1 of 3 at 95 s: no cycle dosed
	two := hakkar(fmt.Sprintf("[30000,3,%d,1]", poison), fmt.Sprintf("[30000,4,%d,1]", poison))
	// two: at 35 s A, B, C and D all carry it (4 of 4: dosed); at 95 s B and C do, A does not (D is dead): 2 of 3.
	rows := Fold([]Input{{Key: "k1", Fight: one}, {Key: "k2", Fight: two}})
	r := row(t, rows, 14834)
	if r.Kind != "hakkar" || r.Name != "Hakkar" || r.Fights != 2 || r.Kills != 2 || r.Wipes != 0 {
		t.Fatalf("row: %+v", r)
	}
	if r.Casts != 4 || r.Cycles != 4 || r.DosedCycles != 1 || r.AuraFights != 2 {
		t.Fatalf("casts %d cycles %d dosed cycles %d aura fights %d, want 4 4 1 2", r.Casts, r.Cycles, r.DosedCycles, r.AuraFights)
	}
	// (2+1+4+2) carried of (4+3+4+3) alive
	near(t, "dosed_pct", r.DosedPct, 9.0/14.0*100)
	near(t, "deaths_per_fight", &r.DeathsPerFight, 1)
	near(t, "son_deaths_per_fight", r.SonDeathsPerFight, 0)
	if r.Worst != "k1" {
		t.Fatalf("worst = %q, want the fight with the lowest dosed share (k1)", r.Worst)
	}
	if r.Lower {
		t.Fatal("an uncapped list is not a lower bound")
	}
}

// A cycle is dosed at 80 % of the living: 4 of 5 counts (the boundary), 7 of 10 does not; the share is per cast, not over the fight.
func TestDosedCycleThreshold(t *testing.T) {
	mk := func(n, withAura int) combat.Fight {
		f := combat.Fight{ID: "x", Boss: 14834, DurMS: 90000, Log: raws(fmt.Sprintf(`{"k":"cast","t":30000,"sid":%d}`, siphon))}
		var auras []string
		for i := 1; i <= n; i++ {
			f.Bots = append(f.Bots, bot(uint32(i), "B"+strconv.Itoa(i), "dps", 0))
			if i <= withAura {
				auras = append(auras, fmt.Sprintf("[1000,%d,%d,1]", i, poison))
			}
		}
		f.Auras = raws(auras...)
		return f
	}
	for _, c := range []struct{ n, on, want int }{{5, 4, 1}, {5, 3, 0}, {10, 8, 1}, {10, 7, 0}, {1, 1, 1}} {
		r := row(t, Fold([]Input{{Key: "k", Fight: mk(c.n, c.on)}}), 14834)
		if r.DosedCycles != c.want {
			t.Errorf("%d of %d carried: dosed cycles = %d, want %d", c.on, c.n, r.DosedCycles, c.want)
		}
	}
	// a cast with nobody alive is a cycle that is not dosed (no division by zero)
	f := mk(2, 2)
	f.Deaths = []combat.FightDeath{{Name: "B1", TMs: 5000}, {Name: "B2", TMs: 6000}}
	r := row(t, Fold([]Input{{Key: "k", Fight: f}}), 14834)
	if r.Cycles != 1 || r.DosedCycles != 0 || r.DosedPct != nil {
		t.Fatalf("all dead: %+v", r)
	}
}

func TestFoldJindoHexOnMT(t *testing.T) {
	f := combat.Fight{
		ID: "j", Boss: 11380, BossName: "Jin'do the Hexxer", DurMS: 60000, Outcome: "wipe",
		// MT is the tank with the largest damage taken (T2), not the first tank and not the healer.
		Bots: []combat.FightBot{bot(1, "T1", "tank", 100), bot(2, "T2", "tank", 500), bot(3, "H", "healer", 9000)},
		Deaths: []combat.FightDeath{
			{Name: "H", TMs: 20000, Killer: "Shade of Jin'do"}, {Name: "T1", TMs: 30000, Killer: "Jin'do the Hexxer"},
			{Name: "X", TMs: 40000, Killer: "Shade of Jin'do"}, {Name: "Y", TMs: 41000, Killer: "Shade of Jin'do"}},
		// Hex on the MT 10-25 s (15 s) plus an apply at 50 s left open: 10 s to the end of the fight. Hex on T1 and a
		// different aura on T2 do not count.
		Auras: raws(fmt.Sprintf("[10000,2,%d,1]", hex), fmt.Sprintf("[25000,2,%d,-1]", hex), fmt.Sprintf("[50000,2,%d,1]", hex),
			fmt.Sprintf("[5000,1,%d,1]", hex), "[1000,2,24306,1]"),
	}
	quiet := f // a second fight with no aura rows: not part of the Hex mean
	quiet.ID, quiet.Auras = "j2", nil
	quiet.Deaths = nil
	r := row(t, Fold([]Input{{Key: "kj", Fight: f}, {Key: "kj2", Fight: quiet}}), 11380)
	if r.Kind != "jindo" || r.Name != "Jin'do" || r.Fights != 2 || r.Wipes != 2 {
		t.Fatalf("row: %+v", r)
	}
	if r.HexFights != 1 {
		t.Fatalf("hex fights = %d, want 1 (only the fight with aura rows)", r.HexFights)
	}
	near(t, "hex_mt_s_per_fight", r.HexMTSPerFight, 25)
	near(t, "shade_deaths_per_fight", r.ShadeDeathsPerFight, 1.5) // 3 over 2 fights
	near(t, "shade_share_pct", r.ShadeSharePct, 75)               // 3 of the 4 deaths
	if r.Worst != "kj" {
		t.Fatalf("worst = %q, want the fight with the most Shade deaths", r.Worst)
	}
	// no tank at all: no main tank, the fight has no Hex number
	nt := f
	nt.Bots = []combat.FightBot{bot(3, "H", "healer", 9000)}
	if r := row(t, Fold([]Input{{Key: "k", Fight: nt}}), 11380); r.HexFights != 0 || r.HexMTSPerFight != nil {
		t.Fatalf("no tank: %+v", r)
	}
}

func TestFoldNoAuraRowsNull(t *testing.T) {
	h := hakkar()
	h.Auras = nil
	j := combat.Fight{ID: "j", Boss: 11380, DurMS: 60000, Bots: []combat.FightBot{bot(1, "T", "tank", 5)}}
	rows := Fold([]Input{{Key: "kh", Fight: h}, {Key: "kj", Fight: j}})
	rh, rj := row(t, rows, 14834), row(t, rows, 11380)
	if rh.DosedPct != nil || rh.Cycles != 0 || rh.AuraFights != 0 || rh.Casts != 2 {
		t.Fatalf("hakkar with no aura rows says nothing about doses: %+v", rh)
	}
	if rj.HexMTSPerFight != nil || rj.HexFights != 0 {
		t.Fatalf("jin'do with no aura rows: hex is null: %+v", rj)
	}
	b, _ := json.Marshal(rj)
	if !strings.Contains(string(b), `"hex_mt_s_per_fight":null`) {
		t.Fatalf("the null is on the wire: %s", b)
	}
}

func TestFoldBossFromEngaged(t *testing.T) {
	f := hakkar()
	f.Boss, f.BossName = 0, ""
	f.Engaged = []combat.EngagedEntry{{E: 1, N: "Trash", C: 3}, {E: 14834, N: "Hakkar", C: 1}}
	rows := Fold([]Input{{Key: "k", Fight: f}})
	if len(rows) != 1 || rows[0].Boss != 14834 || rows[0].Kind != "hakkar" {
		t.Fatalf("boss 0 with Hakkar engaged is a Hakkar row: %+v", rows)
	}
	// trash (no boss, no preset entry engaged) is not on the scorecard
	trash := combat.Fight{ID: "t", DurMS: 90000, Engaged: []combat.EngagedEntry{{E: 7, N: "Mob", C: 4}}}
	if got := Fold([]Input{{Key: "k", Fight: trash}}); len(got) != 0 {
		t.Fatalf("trash: %+v", got)
	}
}

func TestFoldGenericAndWindow(t *testing.T) {
	mk := func(id string, dur uint32, outcome string, deaths int) Input {
		f := combat.Fight{ID: id, Boss: 4000, BossName: "Some Boss", DurMS: dur, Outcome: outcome}
		for i := 0; i < deaths; i++ {
			f.Deaths = append(f.Deaths, combat.FightDeath{Name: "B" + strconv.Itoa(i), TMs: 1000})
		}
		return Input{Key: id, Fight: f}
	}
	rows := Fold([]Input{mk("a", 70000, "kill", 1), mk("b", 80000, "wipe", 5), mk("c", 59999, "wipe", 9), mk("d", 60000, "end", 0)})
	r := row(t, rows, 4000)
	if r.Kind != "generic" || r.Name != "Some Boss" {
		t.Fatalf("row: %+v", r)
	}
	// "c" is 59.999 s: not counted; "d" is exactly 60 s: counted
	if r.Fights != 3 || r.Kills != 1 || r.Wipes != 1 {
		t.Fatalf("fights %d kills %d wipes %d, want 3 1 1", r.Fights, r.Kills, r.Wipes)
	}
	near(t, "deaths_per_fight", &r.DeathsPerFight, 2)
	if r.Worst != "b" {
		t.Fatalf("worst = %q, want the fight with the most deaths", r.Worst)
	}
	// nobody died: nothing stands out
	if r := row(t, Fold([]Input{mk("e", 90000, "kill", 0)}), 4000); r.Worst != "" {
		t.Fatalf("worst = %q with no deaths", r.Worst)
	}
}

func TestIntervalsRules(t *testing.T) {
	f := combat.Fight{DurMS: 1000, Auras: raws(
		"[500,1,9,1]", "[200,1,9,1]", "[900,1,9,-1]", // the second apply while open is ignored; sorted first
		"[5000,2,9,1]", // clamps to 1000: an instant
		"[-30,2,9,-1]", // clamps to 0: an instant
		"[300,3,9,-1]", // a remove with none open: on from the pull
		"[100,4,9,1]",  // open at the end
		`[1,"x"]`, `"x"`, `[1,2,3]`, `null`, "[1,-4,9,1]", "[1,1.5,9,1]", "[1,4294967296,9,1]")}
	iv := Intervals(f)
	span := func(g uint32) []Span { return iv.BySid[9][g] }
	if s := span(1); len(s) != 1 || s[0] != (Span{200, 900}) {
		t.Fatalf("guid 1: %v", s)
	}
	if s := span(2); len(s) != 0 {
		t.Fatalf("guid 2 is an instant at each end: %v", s)
	}
	if s := span(3); len(s) != 1 || s[0] != (Span{0, 300}) {
		t.Fatalf("guid 3 was on at the pull: %v", s)
	}
	if s := span(4); len(s) != 1 || s[0] != (Span{100, 1000}) {
		t.Fatalf("guid 4 closes at dur_ms: %v", s)
	}
	if iv.Rows != 7 || iv.Capped {
		t.Fatalf("rows %d capped %v: the junk rows are skipped", iv.Rows, iv.Capped)
	}
}

func TestIntervalsCapped(t *testing.T) {
	var rows []string
	for k := 0; k < 1000; k++ {
		rows = append(rows, fmt.Sprintf("[%d,1,7,%d]", 1000+k, map[bool]int{true: 1, false: -1}[k%2 == 0]))
	}
	rows[0] = "[1000,1,7,-1]" // first kept row is a remove: its apply was lost
	f := combat.Fight{DurMS: 5000, Auras: raws(rows...)}
	iv := Intervals(f)
	if !iv.Capped || iv.CapFrom != 1000 {
		t.Fatalf("capped %v from %d", iv.Capped, iv.CapFrom)
	}
	if !iv.unknownAt(7, 1, 500) || iv.unknownAt(7, 1, 2000) || !iv.unknownAt(7, 99, 4000) {
		t.Fatal("before the earliest kept row, and always for a bot with no kept row, is unknown")
	}
	if Intervals(combat.Fight{DurMS: 5000, Auras: raws(rows[1:]...)}).Capped {
		t.Fatal("999 rows are not the cap")
	}
	// Hakkar: a bot the capped list never mentions is "unknown", not "did not carry it": the share is a lower bound
	h := hakkar()
	h.Auras = nil
	for k := 0; k < 1000; k++ {
		h.Auras = append(h.Auras, json.RawMessage(fmt.Sprintf("[%d,1,%d,%d]", 1000+k, poison, map[bool]int{true: 1, false: -1}[k%2 == 0])))
	}
	if r := row(t, Fold([]Input{{Key: "k", Fight: h}}), 14834); !r.Lower {
		t.Fatalf("a capped list makes the row a lower bound: %+v", r)
	}
}

func TestCastsByIDThenName(t *testing.T) {
	f := combat.Fight{Log: raws(
		`{"k":"cast","t":9000,"s":"Blood Siphon","sid":24324}`,
		`{"k":"cast","t":3000,"s":"blood siphon"}`,              // no id: matched by name, any case
		`{"k":"cast","t":4000,"s":"Blood Siphon","sid":1}`,      // another id: the id decides
		`{"k":"death","t":5000,"s":"Blood Siphon","sid":24324}`, // not a cast
		`{"k":"cast","t":-5,"sid":24324}`, `{"k":"cast","sid":24324}`, `{"k":"cast","t":1e300,"sid":24324}`, `"x"`, `7`, `{`)}
	got := Casts(f, 24324, "Blood Siphon")
	if len(got) != 2 || got[0] != 3000 || got[1] != 9000 {
		t.Fatalf("casts: %v", got)
	}
}

// Hostile aura rows, log events and deaths never panic and never run unbounded.
func TestFoldHostileInput(t *testing.T) {
	f := hakkar()
	f.Auras = append(f.Auras, raws("[1e300,1,24321,1]", "[1,1,1e300,1]", "[NaN]", "[]", "{}", `[null,null,null,null]`)...)
	f.Log = append(f.Log, raws(`{"k":"cast","t":"x"}`, `{"k":["a"]}`, `[1]`)...)
	f.Deaths = append(f.Deaths, combat.FightDeath{Name: "", TMs: math.MaxUint32})
	f.Bots = append(f.Bots, combat.FightBot{})
	for i := 0; i < maxAuraRows+500; i++ {
		f.Auras = append(f.Auras, json.RawMessage("[1,1,24321,1]"))
	}
	for i := 0; i < maxLogRows+500; i++ {
		f.Log = append(f.Log, json.RawMessage(fmt.Sprintf(`{"k":"cast","t":%d,"sid":24324}`, i)))
	}
	f.DurMS = math.MaxUint32
	rows := Fold([]Input{{Key: "k", Fight: f}})
	if len(rows) != 1 {
		t.Fatalf("rows: %+v", rows)
	}
	if r := rows[0]; r.Casts > maxLogRows+2 {
		t.Fatalf("the log scan is bounded: %d casts", r.Casts)
	}
	if got := Fold(nil); len(got) != 0 {
		t.Fatalf("no fights: %+v", got)
	}
	// more bosses than the answer may hold
	var in []Input
	for i := 1; i <= maxRows+20; i++ {
		in = append(in, Input{Key: "k", Fight: combat.Fight{Boss: uint32(5000 + i), DurMS: 70000}})
	}
	if got := Fold(in); len(got) != maxRows {
		t.Fatalf("rows = %d, want the %d cap", len(got), maxRows)
	}
}

func readJS(t *testing.T, name string) string {
	t.Helper()
	b, err := os.ReadFile("../../web/static/js/" + name)
	if err != nil {
		t.Skipf("%s is not in this tree: %v", name, err)
	}
	return string(b)
}

// The Go presets and the page's (dash-2's FightAuras.PRESETS) name the same spells.
func TestPresetsMatchFightAuras(t *testing.T) {
	src := readJS(t, "fightauras.js")
	hak := regexp.MustCompile(`14834:\s*\{\s*auras:\s*\[(\d+)[^\]]*\],\s*marker:\s*\{\s*sid:\s*(\d+)`).FindStringSubmatch(src)
	jin := regexp.MustCompile(`11380:\s*\{\s*auras:\s*\[(\d+)`).FindStringSubmatch(src)
	if hak == nil || jin == nil {
		t.Fatal("the PRESETS literal of fightauras.js changed shape: update this test's regex")
	}
	h, j := Presets[EntryHakkar], Presets[EntryJindo]
	if hak[1] != strconv.Itoa(h.AuraSid) || hak[2] != strconv.Itoa(h.MarkerSid) {
		t.Fatalf("Hakkar: fightauras.js has aura %s marker %s, Go has %d and %d", hak[1], hak[2], h.AuraSid, h.MarkerSid)
	}
	if jin[1] != strconv.Itoa(j.AuraSid) {
		t.Fatalf("Jin'do: fightauras.js has aura %s, Go has %d", jin[1], j.AuraSid)
	}
}

// DOSED_SHARE in fdmech.js and DosedShare here are one number, and the page's boss entries and add names are the preset's.
func TestDosedShareMatchesJS(t *testing.T) {
	src := readJS(t, "fdmech.js")
	m := regexp.MustCompile(`DOSED_SHARE\s*=\s*([0-9.]+)`).FindStringSubmatch(src)
	if m == nil {
		t.Fatal("fdmech.js has no DOSED_SHARE literal")
	}
	v, err := strconv.ParseFloat(m[1], 64)
	if err != nil || v != DosedShare {
		t.Fatalf("fdmech.js DOSED_SHARE = %s, Go DosedShare = %v", m[1], DosedShare)
	}
	for _, c := range []struct{ name, re string }{
		{"HAKKAR", strconv.Itoa(EntryHakkar)}, {"JINDO", strconv.Itoa(EntryJindo)},
		{"SHADE_NAME", `"` + Presets[EntryJindo].Killer + `"`}, {"SON_NAME", `"` + Presets[EntryHakkar].Killer + `"`},
	} {
		if !regexp.MustCompile(c.name + `\s*=\s*` + regexp.QuoteMeta(c.re)).MatchString(src) {
			t.Errorf("fdmech.js %s is not %s", c.name, c.re)
		}
	}
}
