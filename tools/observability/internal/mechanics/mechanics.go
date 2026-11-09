// Package mechanics folds the night's boss fights into one scorecard row per boss: how many fights, kills and wipes, and a
// key mechanic per boss (Hakkar: how many of the raid carried Poisonous Blood at each Blood Siphon; Jin'do: Hex time on the
// main tank and Shade deaths). The page draws the same numbers for one fight in web/static/js/fdmech.js (FdMech); DosedShare
// and the Presets' spell ids are pinned equal to the page's by tests. Our own design on our own data: the rows are the
// module's watched-aura rows and boss-cast log of each filed fight (Fight.Auras, Fight.Log).
package mechanics

import (
	"encoding/json"
	"math"
	"sort"
	"strings"

	"tortoise-observability/internal/combat"
)

// DosedShare is the share of the members alive at a Blood Siphon cast that must carry Poisonous Blood for the cycle to count
// as dosed. The page's DOSED_SHARE (web/static/js/fdmech.js) is the same number; a test fails when they differ.
const DosedShare = 0.8

// MinFightMS is the shortest fight the scorecard counts.
const MinFightMS = 60000

// Entries and names the scorecard has presets for (the page's HAKKAR, JINDO and SHADE_NAME read the same).
const (
	EntryHakkar = 14834
	EntryJindo  = 11380
)

// Bounds on untrusted input: no fight, however malformed, makes the fold do more than this much work.
const (
	maxLogRows  = 5000
	maxAuraRows = 20000
	maxRows     = 100 // boss entries in one answer
	capRows     = 1000
)

// Preset is what the scorecard knows about one boss: the aura it reads, the boss cast it marks and the add that kills.
type Preset struct {
	Name      string // the row's name
	MarkerSid int    // the boss cast whose moment the aura is read at (0: none)
	Marker    string // that cast's name, used when an event carries no spell id
	AuraSid   int    // the aura the scorecard reads
	Aura      string
	Killer    string // the add whose kills the row counts
}

// Presets is keyed by boss entry. Keep the ids identical to PRESETS in web/static/js/fightauras.js (TestPresetsMatchFightAuras).
var Presets = map[uint32]Preset{
	EntryHakkar: {Name: "Hakkar", MarkerSid: 24324, Marker: "Blood Siphon", AuraSid: 24321, Aura: "Poisonous Blood", Killer: "Son of Hakkar"},
	EntryJindo:  {Name: "Jin'do", AuraSid: 17172, Aura: "Hex", Killer: "Shade of Jin'do"},
}

// presetOrder is the entries in a fixed order, so "the first preset entry in engaged" does not depend on map order.
var presetOrder = []uint32{EntryHakkar, EntryJindo}

// Span is one stretch an aura was on, in fight milliseconds.
type Span struct{ From, To int64 }

// AuraIntervals is Intervals' answer: the stretches per aura and per bot guid.
type AuraIntervals struct {
	BySid    map[int]map[uint32][]Span
	RowGuids map[int]map[uint32]bool // the bots that have a kept row of the aura
	Rows     int                     // the usable rows
	Capped   bool                    // the list is at the module's cap: its oldest rows are lost
	CapFrom  int64                   // nothing is known before this when Capped
	DurMS    int64
}

// Intervals turns a fight's aura rows into uptime intervals, by the rules of FightAuras.intervals (fightauras.js), which read:
//
//	Intervals of every (spell, bot) from [t_ms, guid, spell id, 1 apply | -1 remove] rows. An apply while one is open is
//	ignored; a remove with none open means the aura was on at the pull ([0, t]); what is still open at the end closes at
//	dur_ms. Times clamp into the fight, rows sort by time (stable), a row that is not four numbers is skipped, an instant is
//	not uptime. A list at the module's cap (1000 rows) has lost its oldest rows: a remove with none open then means "up from
//	the earliest kept row", and nothing is known before that row.
func Intervals(f combat.Fight) AuraIntervals {
	dur := int64(f.DurMS)
	out := AuraIntervals{BySid: map[int]map[uint32][]Span{}, RowGuids: map[int]map[uint32]bool{}, DurMS: dur}
	type row struct {
		t    float64
		guid uint32
		sid  int
		add  bool
	}
	var rows []row
	for i, raw := range f.Auras {
		if i >= maxAuraRows {
			break
		}
		var r []float64
		if json.Unmarshal(raw, &r) != nil || len(r) < 4 {
			continue
		}
		g, s := r[1], r[2]
		if g < 0 || g > math.MaxUint32 || g != math.Floor(g) || s < 0 || s > math.MaxInt32 || s != math.Floor(s) {
			continue
		}
		rows = append(rows, row{t: r[0], guid: uint32(g), sid: int(s), add: r[3] > 0})
	}
	sort.SliceStable(rows, func(i, j int) bool { return rows[i].t < rows[j].t })
	out.Rows = len(rows)
	clamp := func(t float64) int64 {
		if t < 0 {
			return 0
		}
		if t > float64(dur) {
			return dur
		}
		return int64(t)
	}
	out.Capped = len(f.Auras) >= capRows
	if out.Capped && len(rows) > 0 {
		out.CapFrom = clamp(rows[0].t)
	}
	type key struct {
		sid  int
		guid uint32
	}
	open := map[key]int64{}
	put := func(k key, from, to int64) {
		if to <= from {
			return
		}
		m := out.BySid[k.sid]
		if m == nil {
			m = map[uint32][]Span{}
			out.BySid[k.sid] = m
		}
		m[k.guid] = append(m[k.guid], Span{from, to})
	}
	for _, r := range rows {
		t, k := clamp(r.t), key{r.sid, r.guid}
		g := out.RowGuids[r.sid]
		if g == nil {
			g = map[uint32]bool{}
			out.RowGuids[r.sid] = g
		}
		g[r.guid] = true
		if r.add {
			if _, on := open[k]; !on {
				open[k] = t
			}
			continue
		}
		if from, on := open[k]; on {
			if t < from {
				t = from
			}
			put(k, from, t)
			delete(open, k)
		} else if out.Capped {
			put(k, out.CapFrom, t)
		} else {
			put(k, 0, t)
		}
	}
	for k, from := range open {
		put(k, from, dur)
	}
	for _, m := range out.BySid {
		for g := range m {
			s := m[g]
			sort.Slice(s, func(i, j int) bool { return s[i].From < s[j].From })
		}
	}
	return out
}

// holds reports whether the spans hold the aura at t.
func holds(spans []Span, t int64) bool {
	for _, s := range spans {
		if s.From <= t && t < s.To {
			return true
		}
	}
	return false
}

// unknownAt reports whether a bot's state at t lies in a stretch a capped list does not tell: before the earliest kept row, or
// at any time for a bot that has no kept row of the aura (it may have held it throughout).
func (iv AuraIntervals) unknownAt(sid int, guid uint32, t int64) bool {
	if !iv.Capped || iv.CapFrom <= 0 {
		return false
	}
	if iv.RowGuids[sid][guid] {
		return t < iv.CapFrom
	}
	return true
}

// spanOn is the aura's time on one bot, in ms.
func (iv AuraIntervals) spanOn(sid int, guid uint32) int64 {
	var n int64
	for _, s := range iv.BySid[sid][guid] {
		n += s.To - s.From
	}
	return n
}

// Input is one filed fight with its key (the link the row's worst fight carries).
type Input struct {
	Key   string
	Fight combat.Fight
}

// Row is one boss of the night. Floats are nil where nothing can be said (no aura rows, no deaths); the preset fields of
// another boss are zero or nil.
type Row struct {
	Boss           uint32  `json:"boss"`
	Name           string  `json:"name"`
	Kind           string  `json:"kind"` // hakkar, jindo or generic
	Fights         int     `json:"fights"`
	Kills          int     `json:"kills"`
	Wipes          int     `json:"wipes"`
	DeathsPerFight float64 `json:"deaths_per_fight"`
	Worst          string  `json:"worst"` // the fight key to open, "" when none stands out

	// Hakkar. Casts are every Blood Siphon cast; cycles and dosed are counted over the fights that have aura rows (AuraFights).
	Casts             int      `json:"casts"`
	AuraFights        int      `json:"aura_fights"`
	Cycles            int      `json:"cycles"`
	DosedCycles       int      `json:"dosed_cycles"`
	DosedPct          *float64 `json:"dosed_pct"`
	SonDeathsPerFight *float64 `json:"son_deaths_per_fight"`

	// Jin'do. HexFights is the fights with aura rows and a main tank; HexMTSPerFight is nil when it is 0.
	HexFights           int      `json:"hex_fights"`
	HexMTSPerFight      *float64 `json:"hex_mt_s_per_fight"`
	ShadeDeathsPerFight *float64 `json:"shade_deaths_per_fight"`
	ShadeSharePct       *float64 `json:"shade_share_pct"`

	// Lower is true when a capped aura list made some number a lower bound.
	Lower bool `json:"lower"`
}

type logEvent struct {
	K   string   `json:"k"`
	T   *float64 `json:"t"`
	S   string   `json:"s"`
	Sid *float64 `json:"sid"`
}

// Casts returns the times of the fight's log casts of one boss spell: by spell id when the event carries one, else by name
// (case-insensitive), as FightAuras.markers does. Oldest first.
func Casts(f combat.Fight, sid int, name string) []int64 {
	var out []int64
	for i, raw := range f.Log {
		if i >= maxLogRows {
			break
		}
		var e logEvent
		if json.Unmarshal(raw, &e) != nil || e.K != "cast" || e.T == nil || *e.T < 0 || *e.T > math.MaxInt32 {
			continue
		}
		bySid := e.Sid != nil && sid != 0 && *e.Sid == float64(sid)
		byName := (e.Sid == nil || sid == 0) && name != "" && strings.EqualFold(e.S, name)
		if bySid || byName {
			out = append(out, int64(*e.T))
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i] < out[j] })
	return out
}

// firstDeaths is the time of each bot's first death by name (a bot stays dead: the module records no resurrection).
func firstDeaths(f combat.Fight) map[string]int64 {
	died := map[string]int64{}
	for _, d := range f.Deaths {
		if _, ok := died[d.Name]; !ok {
			died[d.Name] = int64(d.TMs)
		}
	}
	return died
}

// BossOf is the entry a fight is filed under: its boss, else the first preset entry among the engaged.
func BossOf(f combat.Fight) uint32 {
	if f.Boss != 0 {
		return f.Boss
	}
	for _, e := range presetOrder {
		for _, g := range f.Engaged {
			if g.E == e {
				return e
			}
		}
	}
	return 0
}

// Dosed counts, for one fight's Blood Siphon casts, the members alive at each cast and how many of them carried the aura. A
// bot whose state lies in a capped list's unknown stretch counts as not carrying it and sets lower.
func Dosed(f combat.Fight, iv AuraIntervals, p Preset) (cycles, dosed, on, alive int, lower bool) {
	died := firstDeaths(f)
	for _, t := range Casts(f, p.MarkerSid, p.Marker) {
		a, o := 0, 0
		for _, b := range f.Bots {
			if d, dead := died[b.Name]; dead && t >= d {
				continue
			}
			a++
			if holds(iv.BySid[p.AuraSid][b.GUID], t) {
				o++
			} else if iv.unknownAt(p.AuraSid, b.GUID, t) {
				lower = true
			}
		}
		cycles++
		alive += a
		on += o
		if a > 0 && float64(o)/float64(a) >= DosedShare {
			dosed++
		}
	}
	return
}

// mainTank is the tank-role bot with the largest damage taken (ties by name).
func mainTank(f combat.Fight) (combat.FightBot, bool) {
	var best combat.FightBot
	found := false
	for _, b := range f.Bots {
		if b.Role != "tank" {
			continue
		}
		if !found || b.Taken > best.Taken || (b.Taken == best.Taken && b.Name < best.Name) {
			best, found = b, true
		}
	}
	return best, found
}

func killedBy(f combat.Fight, killer string) int {
	n := 0
	for _, d := range f.Deaths {
		if d.Killer == killer {
			n++
		}
	}
	return n
}

func ptr(v float64) *float64 { return &v }

// acc is one boss's running sums.
type acc struct {
	row                    Row
	deaths                 int
	sonDeaths, shadeDeaths int
	sumOn, sumAlive        int
	hexMS                  int64
	worstKey               string
	worstScore             float64
	worstSet               bool
}

// Fold groups the fights (at least MinFightMS long) by boss and returns one row per boss, the most fought first. A fight
// with no boss and no preset entry among its engaged units (trash) is not on the scorecard.
func Fold(in []Input) []Row {
	by := map[uint32]*acc{}
	var order []uint32
	for _, it := range in {
		f := it.Fight
		if f.DurMS < MinFightMS {
			continue
		}
		boss := BossOf(f)
		if boss == 0 {
			continue
		}
		a := by[boss]
		if a == nil {
			if len(by) >= maxRows {
				continue
			}
			a = &acc{row: Row{Boss: boss, Kind: "generic", Name: f.BossName}}
			if p, ok := Presets[boss]; ok {
				a.row.Name = p.Name
				a.row.Kind = map[uint32]string{EntryHakkar: "hakkar", EntryJindo: "jindo"}[boss]
			}
			by[boss] = a
			order = append(order, boss)
		}
		if a.row.Name == "" {
			a.row.Name = f.BossName
		}
		a.row.Fights++
		switch f.Outcome {
		case "kill":
			a.row.Kills++
		case "wipe":
			a.row.Wipes++
		}
		a.deaths += len(f.Deaths)
		// "Worst" takes the fight with the most deaths unless the preset has its own measure below.
		score := -float64(len(f.Deaths))
		measured := len(f.Deaths) > 0
		iv := Intervals(f)
		switch a.row.Kind {
		case "hakkar":
			p := Presets[boss]
			casts := Casts(f, p.MarkerSid, p.Marker)
			a.row.Casts += len(casts)
			a.sonDeaths += killedBy(f, p.Killer)
			measured = false
			if iv.Rows > 0 {
				a.row.AuraFights++
				cycles, dosed, on, alive, lower := Dosed(f, iv, p)
				a.row.Cycles += cycles
				a.row.DosedCycles += dosed
				a.sumOn += on
				a.sumAlive += alive
				a.row.Lower = a.row.Lower || lower
				if alive > 0 {
					score, measured = float64(on)/float64(alive), true
				}
			}
		case "jindo":
			p := Presets[boss]
			shade := killedBy(f, p.Killer)
			a.shadeDeaths += shade
			score, measured = -float64(shade), shade > 0
			if mt, ok := mainTank(f); ok && iv.Rows > 0 {
				a.row.HexFights++
				a.hexMS += iv.spanOn(p.AuraSid, mt.GUID)
				if iv.Capped {
					a.row.Lower = true
				}
			}
		}
		if measured && (!a.worstSet || score < a.worstScore) {
			a.worstKey, a.worstScore, a.worstSet = it.Key, score, true
		}
	}
	rows := make([]Row, 0, len(order))
	for _, boss := range order {
		a := by[boss]
		r := a.row
		n := float64(r.Fights)
		r.DeathsPerFight = float64(a.deaths) / n
		r.Worst = a.worstKey
		switch r.Kind {
		case "hakkar":
			r.SonDeathsPerFight = ptr(float64(a.sonDeaths) / n)
			if a.sumAlive > 0 {
				r.DosedPct = ptr(float64(a.sumOn) / float64(a.sumAlive) * 100)
			}
		case "jindo":
			r.ShadeDeathsPerFight = ptr(float64(a.shadeDeaths) / n)
			if a.deaths > 0 {
				r.ShadeSharePct = ptr(float64(a.shadeDeaths) / float64(a.deaths) * 100)
			}
			if r.HexFights > 0 {
				r.HexMTSPerFight = ptr(float64(a.hexMS) / 1000 / float64(r.HexFights))
			}
		}
		rows = append(rows, r)
	}
	sort.SliceStable(rows, func(i, j int) bool {
		if rows[i].Fights != rows[j].Fights {
			return rows[i].Fights > rows[j].Fights
		}
		return rows[i].Boss < rows[j].Boss
	})
	return rows
}
