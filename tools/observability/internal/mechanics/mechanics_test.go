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
	"time"

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
	quiet := f // a second fight with no aura rows in a night whose watch is deployed: Hex on the MT for 0 s, counted
	quiet.ID, quiet.Auras = "j2", nil
	quiet.Deaths = nil
	r := row(t, Fold([]Input{{Key: "kj", Fight: f}, {Key: "kj2", Fight: quiet}}), 11380)
	if r.Kind != "jindo" || r.Name != "Jin'do" || r.Fights != 2 || r.Wipes != 2 {
		t.Fatalf("row: %+v", r)
	}
	if r.HexFights != 2 {
		t.Fatalf("hex fights = %d, want 2 (the rowless fight counts with 0 s)", r.HexFights)
	}
	near(t, "hex_mt_s_per_fight", r.HexMTSPerFight, 12.5)
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
	got, cut := Casts(f, 24324, "Blood Siphon")
	if cut {
		t.Fatal("nothing was cut")
	}
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
	if r := rows[0]; r.Casts > maxCasts || !r.Lower {
		t.Fatalf("the casts are bounded and the row says so: %d casts, lower %v", r.Casts, r.Lower)
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

// A night whose watch is deployed (some fight has aura rows) counts a fight with none as measured with nobody dosed: a fight where
// nobody carried the aura writes no rows, and it is the worst one.
func TestFoldRowlessFightInDeployedNight(t *testing.T) {
	dosed := hakkar(fmt.Sprintf("[30000,3,%d,1]", poison), fmt.Sprintf("[30000,4,%d,1]", poison)) // 4/4 at 35 s, 2/3 at 95 s
	none := hakkar()
	none.ID, none.Auras = "none", nil // 0/4 and 0/3
	r := row(t, Fold([]Input{{Key: "kd", Fight: dosed}, {Key: "kn", Fight: none}}), 14834)
	if r.Worst != "kn" {
		t.Fatalf("worst = %q, want the rowless fight kn", r.Worst)
	}
	if r.Cycles != 4 || r.DosedCycles != 1 || r.AuraFights != 1 {
		t.Fatalf("cycles %d dosed %d aura fights %d, want 4 1 1", r.Cycles, r.DosedCycles, r.AuraFights)
	}
	near(t, "dosed_pct", r.DosedPct, 6.0/14.0*100) // (4+2) carried of (4+3+4+3) alive: the rowless fight's casts count at 0

	// Jin'do: the rowless fight is 0 s on the MT, not excluded from the mean
	j := func(id string, auras ...string) Input {
		return Input{Key: id, Fight: combat.Fight{ID: id, Boss: 11380, DurMS: 60000, Bots: []combat.FightBot{bot(1, "T", "tank", 5)}, Auras: raws(auras...)}}
	}
	jr := row(t, Fold([]Input{j("a", fmt.Sprintf("[10000,1,%d,1]", hex), fmt.Sprintf("[30000,1,%d,-1]", hex)), j("b")}), 11380)
	if jr.HexFights != 2 {
		t.Fatalf("hex fights %d, want 2", jr.HexFights)
	}
	near(t, "hex_mt_s_per_fight", jr.HexMTSPerFight, 10)
	// a night with no rows at all stays null (TestFoldNoAuraRowsNull)
}

// One boss rule: the boss, else the boss phase's entry, else the first preset entry among the engaged (the page's FdMech reads the same).
func TestBossOfRule(t *testing.T) {
	engaged := []combat.EngagedEntry{{E: 5, N: "Mob"}, {E: 11380, N: "Jin'do"}, {E: 14834, N: "Hakkar"}}
	for _, c := range []struct {
		name string
		f    combat.Fight
		want uint32
	}{
		{"boss first", combat.Fight{Boss: 4000, BossPhaseEntry: 14834, Engaged: engaged}, 4000},
		{"phase entry second (not the preset the engaged list would give)", combat.Fight{BossPhaseEntry: 11380, Engaged: engaged}, 11380},
		{"a phase entry that is no preset is still the entry", combat.Fight{BossPhaseEntry: 999, Engaged: engaged}, 999},
		{"preset among engaged third (the first preset in the fixed order)", combat.Fight{Engaged: engaged}, 14834},
		{"no preset engaged", combat.Fight{Engaged: []combat.EngagedEntry{{E: 5}}}, 0},
	} {
		if got := BossOf(c.f); got != c.want {
			t.Errorf("%s: BossOf = %d, want %d", c.name, got, c.want)
		}
	}
}

// Per-fight bounds: a raid past 80 members and a log past 500 casts are read from what the caps let through, and the row says so.
func TestDosedBoundsAndBudget(t *testing.T) {
	f := combat.Fight{ID: "x", Boss: 14834, DurMS: 3600000}
	for i := 1; i <= 5000; i++ {
		f.Bots = append(f.Bots, bot(uint32(i), "B"+strconv.Itoa(i), "dps", 0))
	}
	for i := 0; i < 5000; i++ {
		f.Log = append(f.Log, json.RawMessage(fmt.Sprintf(`{"k":"cast","t":%d,"sid":%d}`, 1000+i, siphon)))
	}
	for i := 0; i < 20000; i++ {
		f.Auras = append(f.Auras, json.RawMessage(fmt.Sprintf("[%d,%d,%d,1]", 1000+i, 1+i%5000, poison)))
	}
	start := time.Now()
	r := row(t, Fold([]Input{{Key: "k", Fight: f}}), 14834)
	if d := time.Since(start); d > time.Second {
		t.Fatalf("a hostile fight took %v", d)
	}
	if r.Casts != maxCasts || r.Cycles != maxCasts || !r.Lower {
		t.Fatalf("casts %d cycles %d lower %v, want %d %d true", r.Casts, r.Cycles, r.Lower, maxCasts, maxCasts)
	}
	// only the first maxBots members are read: alive per cast is at most 80
	if got, ok := Casts(f, siphon, "Blood Siphon"); !ok || len(got) != maxCasts {
		t.Fatalf("casts: %d, cut %v", len(got), ok)
	}
	_, _, _, alive, lower := Dosed(f, Intervals(f), Presets[EntryHakkar], []int64{5000})
	if alive != maxBots || !lower {
		t.Fatalf("alive %d lower %v, want %d true", alive, lower, maxBots)
	}
	// within the caps nothing is marked
	small := hakkar()
	if r := row(t, Fold([]Input{{Key: "k", Fight: small}}), 14834); r.Lower {
		t.Fatal("a normal fight is not a lower bound")
	}
}

// A death with no time (the typed field reads it as 0) is skipped, as the page skips a row without a numeric t_ms: it must not kill the
// bot at the pull.
func TestFirstDeathsSkipsNoTime(t *testing.T) {
	f := hakkar()
	f.Deaths = []combat.FightDeath{{Name: "A"}, {Name: "D", TMs: 60000}}
	died := firstDeaths(f)
	if _, bad := died["A"]; bad || died["D"] != 60000 {
		t.Fatalf("first deaths: %v", died)
	}
	// so A is alive at the 35 s cast: 4 alive, A (10-40 s) and B carry it
	_, _, on, alive, _ := Dosed(f, Intervals(f), Presets[EntryHakkar], []int64{35000})
	if on != 2 || alive != 4 {
		t.Fatalf("on %d alive %d, want 2 of 4", on, alive)
	}
}

// One main-tank rule in the daemon and for the page's filed fights (fdmech_check.js reads the same file): the tank with the largest damage
// taken, ties by name, whatever the raid layout names.
func TestMainTankSharedFixture(t *testing.T) {
	b, err := os.ReadFile("testdata/mt-rule.json")
	if err != nil {
		t.Fatal(err)
	}
	var fx struct {
		Fight  combat.Fight      `json:"fight"`
		Layout map[string]string `json:"layout"`
		Want   struct {
			MT   string  `json:"mt"`
			HexS float64 `json:"hex_s"`
		} `json:"want"`
	}
	if err := json.Unmarshal(b, &fx); err != nil {
		t.Fatal(err)
	}
	if fx.Layout["mt"] == fx.Want.MT || fx.Layout["mt"] == "" {
		t.Fatal("the fixture's layout must name another tank than the one with the most damage taken")
	}
	mt, ok := MainTank(fx.Fight)
	if !ok || mt.Name != fx.Want.MT {
		t.Fatalf("main tank = %q, want %q", mt.Name, fx.Want.MT)
	}
	r := row(t, Fold([]Input{{Key: "k", Fight: fx.Fight}}), 11380)
	near(t, "hex_mt_s_per_fight", r.HexMTSPerFight, fx.Want.HexS)
	// ties go to the name
	tie := combat.Fight{Bots: []combat.FightBot{bot(2, "B", "tank", 7), bot(1, "A", "tank", 7)}}
	if mt, _ := MainTank(tie); mt.Name != "A" {
		t.Fatalf("tie: %q", mt.Name)
	}
}

// The watch is deployed from the boss's first fight with aura rows: [rowless 20:00, rows 21:00, rowless 22:00] reads the 22:00 fight as
// 0 % dosed (it ran with the watch and nobody carried the aura) and leaves the 20:00 fight out (it ran before the watch existed).
func TestFoldDeployStartsAtFirstRows(t *testing.T) {
	at := func(f combat.Fight, id string, start int64) Input {
		f.ID, f.Start = id, start
		return Input{Key: id, Fight: f}
	}
	rowless := hakkar()
	rowless.Auras = nil
	r := row(t, Fold([]Input{at(rowless, "k20", 72000), at(hakkar(), "k21", 75600), at(rowless, "k22", 79200)}), 14834)
	if r.Cycles != 4 || r.AuraFights != 1 || r.Casts != 6 {
		t.Fatalf("cycles %d aura fights %d casts %d, want 4 1 6 (k21 and k22 measured, k20 only has its casts counted)", r.Cycles, r.AuraFights, r.Casts)
	}
	near(t, "dosed_pct", r.DosedPct, 3.0/14.0*100) // k21: 3 of 7 alive carried it; k22: 0 of 7
	if r.Worst != "k22" {
		t.Fatalf("worst = %q, want the 22:00 fight (0 %%), not the 20:00 one that ran without the watch", r.Worst)
	}
	// Jin'do: the 20:00 fight is left out of the Hex mean, the 22:00 one counts 0 s
	j := func(id string, start int64, auras ...string) Input {
		return Input{Key: id, Fight: combat.Fight{ID: id, Boss: 11380, Start: start, DurMS: 60000, Bots: []combat.FightBot{bot(1, "T", "tank", 5)}, Auras: raws(auras...)}}
	}
	jr := row(t, Fold([]Input{j("a", 72000), j("b", 75600, fmt.Sprintf("[10000,1,%d,1]", hex), fmt.Sprintf("[40000,1,%d,-1]", hex)), j("c", 79200)}), 11380)
	if jr.HexFights != 2 {
		t.Fatalf("hex fights %d, want 2 (b and c)", jr.HexFights)
	}
	near(t, "hex_mt_s_per_fight", jr.HexMTSPerFight, 15)
	// the order the fights are given in does not matter: the window's first fight with rows decides, by start
	r2 := row(t, Fold([]Input{at(rowless, "k22", 79200), at(hakkar(), "k21", 75600), at(rowless, "k20", 72000)}), 14834)
	if r2.Cycles != 4 || r2.Worst != "k22" {
		t.Fatalf("shuffled: cycles %d worst %q", r2.Cycles, r2.Worst)
	}
}

// Every cap qualifies every number it touches: 1001 deaths are read as 1000 (totals, the worst pick and the shares alike) and the row is
// a lower bound; a main tank after member 80 is not seen, and the Jin'do row says so.
func TestCapsSetLowerEverywhere(t *testing.T) {
	deaths := func(n int, killer string) []combat.FightDeath {
		var d []combat.FightDeath
		for i := 0; i < n; i++ {
			d = append(d, combat.FightDeath{Name: "B" + strconv.Itoa(i), TMs: uint32(1000 + i), Killer: killer})
		}
		return d
	}
	g := Fold([]Input{{Key: "g", Fight: combat.Fight{ID: "g", Boss: 4000, DurMS: 70000, Deaths: deaths(1001, "Boss")}}})
	r := row(t, g, 4000)
	if !r.Lower {
		t.Fatal("1001 deaths: the row is a lower bound")
	}
	near(t, "deaths_per_fight", &r.DeathsPerFight, maxDeaths) // the bounded count, not 1001
	if r.Worst != "g" {
		t.Fatalf("worst %q", r.Worst)
	}
	if row(t, Fold([]Input{{Key: "g", Fight: combat.Fight{ID: "g", Boss: 4000, DurMS: 70000, Deaths: deaths(1000, "Boss")}}}), 4000).Lower {
		t.Fatal("1000 deaths fit the cap")
	}
	// Shade deaths: the 1001st (a Shade kill) is past the cap, so the count and the share are consistent over the 1000 read
	d := deaths(1000, "Jin'do the Hexxer")
	d[0].Killer = "Shade of Jin'do"
	d = append(d, combat.FightDeath{Name: "late", TMs: 5000, Killer: "Shade of Jin'do"})
	jf := combat.Fight{ID: "j", Boss: 11380, DurMS: 60000, Deaths: d, Bots: []combat.FightBot{bot(1, "T", "tank", 5)}}
	jr := row(t, Fold([]Input{{Key: "j", Fight: jf}}), 11380)
	near(t, "shade_deaths_per_fight", jr.ShadeDeathsPerFight, 1)
	near(t, "shade_share_pct", jr.ShadeSharePct, 0.1) // 1 of the 1000 read
	if !jr.Lower {
		t.Fatal("Jin'do with 1001 deaths: lower")
	}
	hf := hakkar()
	hf.Deaths = deaths(1001, "Son of Hakkar")
	if hr := row(t, Fold([]Input{{Key: "h", Fight: hf}}), 14834); !hr.Lower {
		t.Fatal("Hakkar with 1001 deaths: lower")
	} else {
		near(t, "son_deaths_per_fight", hr.SonDeathsPerFight, maxDeaths)
	}

	// a main tank past member 80: the first 80 are read, the winner is not seen and the row says so
	bots := []combat.FightBot{bot(1, "T1", "tank", 5)}
	for i := 2; i <= 100; i++ {
		bots = append(bots, bot(uint32(i), "B"+strconv.Itoa(i), "dps", 0))
	}
	bots[89] = bot(90, "T90", "tank", 9000)
	jf = combat.Fight{ID: "j", Boss: 11380, DurMS: 60000, Bots: bots, Auras: raws(
		fmt.Sprintf("[10000,1,%d,1]", hex), fmt.Sprintf("[20000,1,%d,-1]", hex), fmt.Sprintf("[10000,90,%d,1]", hex), fmt.Sprintf("[40000,90,%d,-1]", hex))}
	jr = row(t, Fold([]Input{{Key: "j", Fight: jf}}), 11380)
	if !jr.Lower {
		t.Fatal("a tank after member 80: lower")
	}
	near(t, "hex_mt_s_per_fight", jr.HexMTSPerFight, 10) // T1 (the first 80), not T90
	if mt, _ := MainTank(jf); mt.Name != "T1" {
		t.Fatalf("main tank %q", mt.Name)
	}
	if row(t, Fold([]Input{{Key: "j", Fight: combat.Fight{ID: "j", Boss: 11380, DurMS: 60000, Bots: bots[:80], Auras: jf.Auras}}}), 11380).Lower {
		t.Fatal("80 members fit the cap")
	}
}

// Only the first four values of an aura row are read, as the page does: anything after them is ignored, and a row of fewer than four
// numbers, or whose first four are not all numbers, is skipped.
func TestIntervalsReadFirstFourValues(t *testing.T) {
	f := combat.Fight{DurMS: 10000, Auras: raws(
		`[1000,1,9,1,"extra"]`, `[5000,1,9,-1,null,{}]`, // read: 1 s to 5 s
		`[1000,2,9,1]`, `[2000,2,9]`, // the second is three values: skipped
		`[1000,3,9,"x"]`, `["1000",4,9,1]`, `[1000,null,9,1]`)}
	iv := Intervals(f)
	if s := iv.BySid[9][1]; len(s) != 1 || s[0] != (Span{1000, 5000}) {
		t.Fatalf("guid 1: %v", s)
	}
	if s := iv.BySid[9][2]; len(s) != 1 || s[0] != (Span{1000, 10000}) {
		t.Fatalf("guid 2 (open at the end, the short row skipped): %v", s)
	}
	if len(iv.BySid[9][3])+len(iv.BySid[9][4]) != 0 || iv.Rows != 3 {
		t.Fatalf("rows %d, the non-numeric ones are skipped: %v", iv.Rows, iv.BySid[9])
	}
}
