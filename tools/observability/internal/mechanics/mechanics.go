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
	// Per fight: the members read (the page's MAX_BOT_ROWS: a raid is at most 40), the casts of one spell and the deaths. A fight over
	// a cap is still folded, from what the cap lets through, and its row says so (Lower).
	maxBots   = 80
	maxCasts  = 500
	maxDeaths = 1000
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

	// Hakkar. Casts are every Blood Siphon cast read; cycles and dosed count every Hakkar fight once any of them has aura rows (the watch
	// is deployed; AuraFights counts the ones that have), and none when no fight has (dosed_pct is then null).
	Casts             int      `json:"casts"`
	AuraFights        int      `json:"aura_fights"`
	Cycles            int      `json:"cycles"`
	DosedCycles       int      `json:"dosed_cycles"`
	DosedPct          *float64 `json:"dosed_pct"`
	SonDeathsPerFight *float64 `json:"son_deaths_per_fight"`

	// Jin'do. HexFights is the fights with a main tank once any Jin'do fight has aura rows (a fight with none counts 0 s); 0 when no fight
	// has, and HexMTSPerFight is then nil.
	HexFights           int      `json:"hex_fights"`
	HexMTSPerFight      *float64 `json:"hex_mt_s_per_fight"`
	ShadeDeathsPerFight *float64 `json:"shade_deaths_per_fight"`
	ShadeSharePct       *float64 `json:"shade_share_pct"`

	// Lower is true when a cap made some number a lower bound: the module's 1000-row aura list, or a fight past the per-fight bounds.
	Lower bool `json:"lower"`
}

type logEvent struct {
	K   string   `json:"k"`
	T   *float64 `json:"t"`
	S   string   `json:"s"`
	Sid *float64 `json:"sid"`
}

// Casts returns the times of the fight's log casts of one boss spell, oldest first: by spell id when the event carries one, else by
// name (case-insensitive), as FightAuras.markers does. At most maxCasts are returned; cut says the log held more (or more rows than
// the scan reads), so the list is a prefix.
func Casts(f combat.Fight, sid int, name string) (out []int64, cut bool) {
	for i, raw := range f.Log {
		if i >= maxLogRows {
			cut = true
			break
		}
		var e logEvent
		if json.Unmarshal(raw, &e) != nil || e.K != "cast" || e.T == nil || *e.T < 0 || *e.T > math.MaxInt32 {
			continue
		}
		bySid := e.Sid != nil && sid != 0 && *e.Sid == float64(sid)
		byName := (e.Sid == nil || sid == 0) && name != "" && strings.EqualFold(e.S, name)
		if !bySid && !byName {
			continue
		}
		if len(out) >= maxCasts {
			cut = true
			break
		}
		out = append(out, int64(*e.T))
	}
	sort.Slice(out, func(i, j int) bool { return out[i] < out[j] })
	return out, cut
}

// firstDeaths is the time of each bot's first death by name (a bot stays dead: the module records no resurrection). A death with no
// time is skipped, as the page does for a row without a numeric t_ms (the typed field reads a missing one as 0, and no death
// happens at the pull itself).
func firstDeaths(f combat.Fight) map[string]int64 {
	died := map[string]int64{}
	for i, d := range f.Deaths {
		if i >= maxDeaths {
			break
		}
		if d.TMs == 0 {
			continue
		}
		if _, ok := died[d.Name]; !ok {
			died[d.Name] = int64(d.TMs)
		}
	}
	return died
}

// BossOf is the entry a fight is filed under, one rule for the daemon and the page (FdMech): its boss, else the boss phase's entry,
// else the first preset entry among the engaged.
func BossOf(f combat.Fight) uint32 {
	if f.Boss != 0 {
		return f.Boss
	}
	if f.BossPhaseEntry != 0 {
		return f.BossPhaseEntry
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

// Dosed counts, for one fight's Blood Siphon casts (read once by the caller), the members alive at each cast and how many of them
// carried the aura. A bot whose state lies in a capped list's unknown stretch counts as not carrying it and sets lower, as does a
// raid over maxBots (the rest are not read).
func Dosed(f combat.Fight, iv AuraIntervals, p Preset, casts []int64) (cycles, dosed, on, alive int, lower bool) {
	died := firstDeaths(f)
	bots := f.Bots
	if len(bots) > maxBots {
		bots, lower = bots[:maxBots], true
	}
	for _, t := range casts {
		a, o := 0, 0
		for _, b := range bots {
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

// MainTank is the one main-tank rule of the night table and of the page's filed fights: the tank-role bot with the largest damage
// taken, ties by name. (The page's running fight has no damage taken yet and reads the layout's tank instead.)
func MainTank(f combat.Fight) (combat.FightBot, bool) {
	var best combat.FightBot
	found := false
	for i, b := range f.Bots {
		if i >= maxBots {
			break
		}
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
	for i, d := range f.Deaths {
		if i >= maxDeaths {
			break
		}
		if d.Killer == killer {
			n++
		}
	}
	return n
}

func ptr(v float64) *float64 { return &v }

// fightData is one counted fight and what is read from it once.
type fightData struct {
	key string
	f   combat.Fight
	iv  AuraIntervals
}

// acc is one boss's fights and running sums.
type acc struct {
	row    Row
	fights []fightData
	deaths int
}

// Fold groups the fights (at least MinFightMS long) by boss and returns one row per boss, the most fought first. A fight with no boss
// and no preset entry among its engaged units (trash) is not on the scorecard.
//
// Whether a boss's watch rows are deployed is decided over the whole window: when any of its fights has aura rows, one without
// counts as measured with nobody dosed (a fight where nobody carried the aura writes no rows, and is the worst one); when none has,
// the dose and Hex numbers are null.
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
		fd := fightData{key: it.Key, f: f}
		if a.row.Kind != "generic" {
			fd.iv = Intervals(f)
		}
		a.fights = append(a.fights, fd)
	}
	rows := make([]Row, 0, len(order))
	for _, boss := range order {
		rows = append(rows, by[boss].finish())
	}
	sort.SliceStable(rows, func(i, j int) bool {
		if rows[i].Fights != rows[j].Fights {
			return rows[i].Fights > rows[j].Fights
		}
		return rows[i].Boss < rows[j].Boss
	})
	return rows
}

// finish reads a boss's fights into its row.
func (a *acc) finish() Row {
	r := a.row
	n := float64(r.Fights)
	r.DeathsPerFight = float64(a.deaths) / n
	deployed := false
	for _, fd := range a.fights {
		if fd.iv.Rows > 0 {
			deployed = true
			r.AuraFights++
		}
	}
	var (
		worstKey               string
		worstScore             float64
		worstSet               bool
		sonDeaths, shadeDeaths int
		sumOn, sumAlive        int
		hexMS                  int64
	)
	consider := func(key string, score float64, measured bool) {
		if measured && (!worstSet || score < worstScore) {
			worstKey, worstScore, worstSet = key, score, true
		}
	}
	for _, fd := range a.fights {
		f, iv := fd.f, fd.iv
		// "Worst" takes the fight with the most deaths unless the preset has its own measure below.
		score, measured := -float64(len(f.Deaths)), len(f.Deaths) > 0
		switch r.Kind {
		case "hakkar":
			p := Presets[r.Boss]
			casts, cut := Casts(f, p.MarkerSid, p.Marker)
			r.Casts += len(casts)
			r.Lower = r.Lower || cut
			sonDeaths += killedBy(f, p.Killer)
			measured = false
			if deployed {
				cycles, dosed, on, alive, lower := Dosed(f, iv, p, casts)
				r.Cycles += cycles
				r.DosedCycles += dosed
				sumOn += on
				sumAlive += alive
				r.Lower = r.Lower || lower
				if alive > 0 {
					score, measured = float64(on)/float64(alive), true
				}
			}
		case "jindo":
			p := Presets[r.Boss]
			shade := killedBy(f, p.Killer)
			shadeDeaths += shade
			score, measured = -float64(shade), shade > 0
			if mt, ok := MainTank(f); ok && deployed {
				r.HexFights++
				hexMS += iv.spanOn(p.AuraSid, mt.GUID)
				if iv.Capped {
					r.Lower = true
				}
			}
		}
		consider(fd.key, score, measured)
	}
	r.Worst = worstKey
	switch r.Kind {
	case "hakkar":
		r.SonDeathsPerFight = ptr(float64(sonDeaths) / n)
		if sumAlive > 0 {
			r.DosedPct = ptr(float64(sumOn) / float64(sumAlive) * 100)
		}
	case "jindo":
		r.ShadeDeathsPerFight = ptr(float64(shadeDeaths) / n)
		if a.deaths > 0 {
			r.ShadeSharePct = ptr(float64(shadeDeaths) / float64(a.deaths) * 100)
		}
		if r.HexFights > 0 {
			r.HexMTSPerFight = ptr(float64(hexMS) / 1000 / float64(r.HexFights))
		}
	}
	return r
}
