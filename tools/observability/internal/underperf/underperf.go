// Package underperf grades one commander's window of stored fights: each
// flagged bot next to the median of its role peers, the warning rules that
// fired on it, and the fights that flagged it.
//
// The numbers are the per-fight figures the module already sends
// (combat.Fight) plus the warnings combat.Judge makes from them; nothing new
// comes from the game server. Range, line of sight and mana are not in the
// data, so nothing here measures them.
package underperf

import (
	"math"
	"sort"
	"strconv"
	"strings"

	"tortoise-observability/internal/combat"
)

// Limits are the named limits of the comparison, both as one default set and
// as the limits field of every answer.
type Limits struct {
	MinJudgedS      int     `json:"min_judged_s"`
	MinAliveS       int     `json:"min_alive_s"`
	MinPeers        int     `json:"min_peers"`
	MinTankPeers    int     `json:"min_tank_peers"`
	MinFights       int     `json:"min_fights"`
	YellowRatio     float64 `json:"yellow_ratio"`
	RedRatio        float64 `json:"red_ratio"`
	IdleRatio       float64 `json:"idle_ratio"`
	DeathRatio      float64 `json:"death_ratio"`
	MinDeaths       int     `json:"min_deaths"`
	EarlyDeathShare float64 `json:"early_death_share"`
	RunGapS         int64   `json:"run_gap_s"`
	MaxFights       int     `json:"max_fights"`
	MaxFightRefs    int     `json:"max_fight_refs"`
	SpellListCap    int     `json:"spell_list_cap"`
}

// DefaultLimits is the table served with every answer.
func DefaultLimits() Limits {
	return Limits{
		MinJudgedS:      20,
		MinAliveS:       10,
		MinPeers:        2,
		MinTankPeers:    1,
		MinFights:       3,
		YellowRatio:     0.5,
		RedRatio:        0.25,
		IdleRatio:       0.4,
		DeathRatio:      2,
		MinDeaths:       3,
		EarlyDeathShare: 0.5,
		RunGapS:         900,
		MaxFights:       500,
		MaxFightRefs:    20,
		SpellListCap:    12,
	}
}

// Floors are the per-role medians a fight must reach before its "low" mark
// counts, read from the rules table's min_role_median (0: no floor).
type Floors struct {
	Dps    int
	Healer int
}

// ParseFloors reads the role floors out of the rules table. The rules judge
// raw output per fight (dmg for no_damage, heal for no_healing), so the floors
// are compared against the same raw medians here.
func ParseFloors(rules []combat.Rule) Floors {
	var f Floors
	for _, r := range rules {
		switch {
		case r.Condition == "no_damage" && strings.EqualFold(r.Role, "dps"):
			f.Dps = r.MinRoleMedian
		case r.Condition == "no_healing" && strings.EqualFold(r.Role, "healer"):
			f.Healer = r.MinRoleMedian
		}
	}
	return f
}

// Input is one stored fight with its warnings and the session it ran in.
type Input struct {
	Key      string
	Day      string
	Session  uint64
	Fight    combat.Fight
	Warnings []combat.Warning
}

// Run is one stretch of fights of one session: a new run starts when the
// session changes or the gap since the previous fight's end passes RunGapS.
type Run struct {
	Since   int64    `json:"since"`
	Until   int64    `json:"until"`
	Fights  int      `json:"fights"`
	Plan    string   `json:"plan"`
	Legs    []string `json:"legs"`
	Session uint64   `json:"session"`
}

// Rates are one bot's own numbers over the window.
type Rates struct {
	Dps     *float64 `json:"dps"`
	Hps     *float64 `json:"hps"`
	Hcps    *float64 `json:"hcps"`
	Cps     *float64 `json:"cps"`
	Skill10 *float64 `json:"skill10"`
	Deaths  int      `json:"deaths"`
}

// Peers are what a median role peer would have done in the bot's seat.
type Peers struct {
	Dps     *float64 `json:"dps"`
	Hps     *float64 `json:"hps"`
	Hcps    *float64 `json:"hcps"`
	Cps     *float64 `json:"cps"`
	Skill10 *float64 `json:"skill10"`
	Deaths  *float64 `json:"deaths"`
}

// Ratio compares the bot with those peers; both are null when it has no peer
// fight to compare over.
type Ratio struct {
	Main *float64 `json:"main"`
	Cps  *float64 `json:"cps"`
}

// Reason is one thing that lists the bot: a fired rule or a peer comparison.
type Reason struct {
	ID     string `json:"id"`
	Level  string `json:"level"`
	Fights int    `json:"fights"`
	Text   string `json:"text"`
}

// FightRef is one fight that flagged the bot.
type FightRef struct {
	Key      string   `json:"key"`
	Day      string   `json:"day"`
	Start    int64    `json:"start"`
	Step     uint32   `json:"step"`
	Leg      string   `json:"leg"`
	BossName string   `json:"boss_name"`
	Outcome  string   `json:"outcome"`
	Rules    []string `json:"rules"`
	Low      bool     `json:"low"`
}

// BotRow is one listed bot.
type BotRow struct {
	Name       string     `json:"name"`
	GUID       uint32     `json:"guid"`
	Class      string     `json:"class"`
	Spec       string     `json:"spec,omitempty"`
	Role       string     `json:"role"`
	Level      string     `json:"level"`
	Fights     int        `json:"fights"`
	Judged     int        `json:"judged"`
	PeerFights int        `json:"peer_fights"`
	Flagged    int        `json:"flagged"`
	You        Rates      `json:"you"`
	Peers      Peers      `json:"peers"`
	Ratio      Ratio      `json:"ratio"`
	Main       string     `json:"main"`
	Reasons    []Reason   `json:"reasons"`
	Causes     []string   `json:"causes"`
	FightRefs  []FightRef `json:"fight_refs"`
}

// OKRow is a bot that was judged and is fine.
type OKRow struct {
	Name   string `json:"name"`
	GUID   uint32 `json:"guid"`
	Class  string `json:"class"`
	Role   string `json:"role"`
	Judged int    `json:"judged"`
}

// Report is the whole answer for one window (and, in the handler's run
// selection, one run of it).
type Report struct {
	Commander string   `json:"commander"`
	Since     int64    `json:"since"`
	Until     int64    `json:"until"`
	Run       string   `json:"run"`
	Fights    int      `json:"fights"`
	Judged    int      `json:"judged"`
	Truncated bool     `json:"truncated"`
	Runs      []Run    `json:"runs"`
	Limits    Limits   `json:"limits"`
	Bots      []BotRow `json:"bots"`
	OK        []OKRow  `json:"ok"`
}

// skillKey names a skill rule: the role and class a spell-count-zero rule
// applies to.
type skillKey struct {
	role  string
	class string
}

// skillRule is one spell_count_zero rule's spell list for a role and class.
type skillRule struct {
	spells []string
}

type aggKey struct {
	guid uint32
	role string
}

// agg is one bot row while the fights are folded in.
type agg struct {
	key   aggKey
	name  string
	class string
	role  string

	fights int
	judged int
	deaths int

	dmg     uint64
	heal    uint64
	casts   uint64
	hc      uint64
	alive   float64
	didNone int

	peerFights    int
	peerAlive     float64
	peerMainNum   float64
	peerCasts     float64
	peerMainExp   float64
	peerCpsExp    float64
	peerDeathsExp float64

	peersDpsExp  float64
	peersHpsExp  float64
	peersHcExp   float64
	peersCpsExp  float64
	peersDeathsN int

	skillCasts     uint64
	skillAlive     float64
	skillPeerExp   float64
	skillPeerAlive float64

	ruleFights map[string]int
	ruleLevel  map[string]string
	ruleText   map[string]string

	flagged     int
	earlyDeaths int
	refs        []FightRef
}

func newAgg(b combat.FightBot) *agg {
	return &agg{
		key: aggKey{b.GUID, b.Role}, name: b.Name, class: b.Class, role: b.Role,
		ruleFights: map[string]int{}, ruleLevel: map[string]string{}, ruleText: map[string]string{},
	}
}

func (a *agg) reasonRule(id string, text string) Reason {
	return Reason{ID: "rule:" + id, Level: a.ruleLevel[id], Fights: a.ruleFights[id], Text: text}
}

func median(vals []float64) float64 {
	if len(vals) == 0 {
		return 0
	}
	cp := append([]float64(nil), vals...)
	sort.Float64s(cp)
	n := len(cp)
	if n%2 == 1 {
		return cp[n/2]
	}
	return (cp[n/2-1] + cp[n/2]) / 2
}

func aliveSeconds(b combat.FightBot, f combat.Fight) float64 {
	dur := float64(f.DurMS) / 1000
	if b.Deaths == 0 {
		return dur
	}
	for _, d := range f.Deaths {
		if d.Name == b.Name {
			if s := float64(d.TMs) / 1000; s < dur {
				return s
			}
			return dur
		}
	}
	// A death count without a death row says nothing about when: count the
	// bot as up for the whole fight.
	return dur
}

// roleKind folds the module's role strings to the three the view compares
// within. Any other string is a dps.
func roleKind(role string) string {
	switch strings.ToLower(role) {
	case "tank":
		return "tank"
	case "healer", "heal":
		return "healer"
	}
	return "dps"
}

func mainRate(b combat.FightBot, alive float64) float64 {
	if alive <= 0 {
		return 0
	}
	switch roleKind(b.Role) {
	case "healer":
		return float64(b.Heal) / alive
	case "tank":
		return float64(b.Casts) / alive
	}
	return float64(b.Dmg) / alive
}

// mainRaw is the main metric's numerator: the same raw output the warning
// rules judge (a role floor is in these units).
func mainRaw(b combat.FightBot) float64 {
	switch roleKind(b.Role) {
	case "healer":
		return float64(b.Heal)
	case "tank":
		return float64(b.Casts)
	}
	return float64(b.Dmg)
}

func mainName(role string) string {
	switch roleKind(role) {
	case "healer":
		return "hps"
	case "tank":
		return "cps"
	}
	return "dps"
}

// isCut reports a bot whose listed spells hit the module's per-bot cap: the
// list cannot prove which spells went uncast.
func isCut(b combat.FightBot, cap int) bool {
	return cap > 0 && len(b.Spells) >= cap
}

func spellCasts(b combat.FightBot, names []string) uint64 {
	var n uint64
	for _, sp := range b.Spells {
		for _, name := range names {
			if sp.N == name {
				n += uint64(sp.C)
			}
		}
	}
	return n
}

// parseSkillRules copies the two spell-count-zero prefixes from
// combat.parseCondition (not exported there) into a role+class -> spells map.
func parseSkillRules(rules []combat.Rule) map[skillKey]skillRule {
	out := map[skillKey]skillRule{}
	for _, r := range rules {
		var spells []string
		if names, ok := strings.CutPrefix(r.Condition, "spell_count_zero:"); ok {
			if name := strings.TrimSpace(names); name != "" {
				spells = []string{name}
			}
		} else if names, ok := strings.CutPrefix(r.Condition, "spells_count_zero:"); ok {
			for _, p := range strings.Split(names, ",") {
				if p = strings.TrimSpace(p); p != "" {
					spells = append(spells, p)
				}
			}
		}
		if len(spells) == 0 {
			continue
		}
		role, class := strings.ToLower(r.Role), strings.ToLower(r.Class)
		if role == "" || class == "" {
			continue
		}
		out[skillKey{role, class}] = skillRule{spells: spells}
	}
	return out
}

// skillRuleFor names the rule's spells for a bot's role and class.
func skillRuleFor(m map[skillKey]skillRule, b combat.FightBot) (skillRule, bool) {
	s, ok := m[skillKey{roleKind(b.Role), strings.ToLower(b.Class)}]
	return s, ok
}

func levelRank(level string) int {
	if level == "red" {
		return 0
	}
	return 1
}

func higherLevel(a, b string) string {
	if a == "red" || b == "red" {
		return "red"
	}
	return "yellow"
}

func roundTo(v float64, digits int) float64 {
	p := math.Pow(10, float64(digits))
	return math.Round(v*p) / p
}

func num(v float64, digits int) *float64 {
	if math.IsNaN(v) || math.IsInf(v, 0) {
		return nil
	}
	r := roundTo(v, digits)
	return &r
}

func plural(n int) string {
	if n == 1 {
		return "1 fight"
	}
	return itoa(n) + " fights"
}

func fmt1(v float64) string {
	return strconv.FormatFloat(roundTo(v, 1), 'f', -1, 64)
}

func fmt2(v float64) string {
	return strconv.FormatFloat(roundTo(v, 2), 'f', -1, 64)
}

func itoa(n int) string { return strconv.Itoa(n) }

// Build folds one window of fights into the report. Fights outside
// [since, until) are dropped; only the newest MaxFights are kept.
func Build(commander string, since, until int64, fights []Input, rules []combat.Rule, floors Floors, lim Limits) Report {
	if lim == (Limits{}) {
		lim = DefaultLimits()
	}
	in := make([]Input, 0, len(fights))
	for _, f := range fights {
		if f.Fight.Start >= since && f.Fight.Start < until {
			in = append(in, f)
		}
	}
	sort.SliceStable(in, func(i, j int) bool {
		if in[i].Fight.Start != in[j].Fight.Start {
			return in[i].Fight.Start < in[j].Fight.Start
		}
		return in[i].Key < in[j].Key
	})
	truncated := false
	if lim.MaxFights > 0 && len(in) > lim.MaxFights {
		in = in[len(in)-lim.MaxFights:]
		truncated = true
	}

	skills := parseSkillRules(rules)
	aggs := map[aggKey]*agg{}
	judgedFights := 0

	for _, input := range in {
		f := input.Fight
		judged := map[uint32]bool{}
		alive := map[uint32]float64{}
		bots := map[uint32]combat.FightBot{}
		names := map[string]uint32{}
		for _, b := range f.Bots {
			bots[b.GUID] = b
			names[b.Name] = b.GUID
			a := aliveSeconds(b, f)
			alive[b.GUID] = a
			if f.DurMS >= uint32(lim.MinJudgedS*1000) && a >= float64(lim.MinAliveS) {
				judged[b.GUID] = true
			}
			k := aggKey{b.GUID, b.Role}
			a2 := aggs[k]
			if a2 == nil {
				a2 = newAgg(b)
				aggs[k] = a2
			}
			a2.name, a2.class = b.Name, b.Class
			a2.fights++
			a2.deaths += int(b.Deaths)
		}
		anyJudged := false
		for _, b := range f.Bots {
			if !judged[b.GUID] {
				continue
			}
			anyJudged = true
			a := alive[b.GUID]
			a2 := aggs[aggKey{b.GUID, b.Role}]
			a2.judged++
			a2.dmg += b.Dmg
			a2.heal += b.Heal
			a2.casts += uint64(b.Casts)
			a2.hc += uint64(b.HealCasts)
			a2.alive += a
			if b.Casts == 0 && b.Dmg == 0 && b.Heal == 0 {
				a2.didNone++
			}

			var pDps, pHps, pHc, pCps, pMain, pDeaths []float64
			for _, p := range f.Bots {
				if p.GUID == b.GUID || p.Role != b.Role || !judged[p.GUID] {
					continue
				}
				pa := alive[p.GUID]
				pDps = append(pDps, float64(p.Dmg)/pa)
				pHps = append(pHps, float64(p.Heal)/pa)
				pHc = append(pHc, float64(p.HealCasts)/pa)
				pCps = append(pCps, float64(p.Casts)/pa)
				pMain = append(pMain, mainRate(p, pa))
				pDeaths = append(pDeaths, float64(p.Deaths))
			}
			need := lim.MinPeers
			if roleKind(b.Role) == "tank" {
				need = lim.MinTankPeers
			}
			if len(pMain) >= need {
				mMain := median(pMain)
				mCps := median(pCps)
				botMain := mainRate(b, a)
				a2.peerFights++
				a2.peerAlive += a
				a2.peerMainNum += botMain * a
				a2.peerCasts += float64(b.Casts)
				a2.peerMainExp += mMain * a
				a2.peerCpsExp += mCps * a
				a2.peerDeathsExp += median(pDeaths)
				a2.peersDpsExp += median(pDps) * a
				a2.peersHpsExp += median(pHps) * a
				a2.peersHcExp += median(pHc) * a
				a2.peersCpsExp += mCps * a
			}

			if sk, ok := skillRuleFor(skills, b); ok && !isCut(b, lim.SpellListCap) {
				a2.skillCasts += spellCasts(b, sk.spells)
				a2.skillAlive += a
				var peerSkills []float64
				for _, p := range f.Bots {
					if p.GUID == b.GUID || p.Role != b.Role || !judged[p.GUID] || isCut(p, lim.SpellListCap) {
						continue
					}
					peerSkills = append(peerSkills, 10*float64(spellCasts(p, sk.spells))/alive[p.GUID])
				}
				if len(peerSkills) > 0 {
					a2.skillPeerExp += median(peerSkills) * a
					a2.skillPeerAlive += a
				}
			}
		}
		if anyJudged {
			judgedFights++
		}

		// The fight's flags: warnings at red or yellow on a bot, and the
		// peer "low" mark for each judged bot under its role's floor.
		flagRules := map[uint32]map[string]bool{}
		low := map[uint32]bool{}
		for _, w := range input.Warnings {
			if w.Level != "red" && w.Level != "yellow" {
				continue
			}
			guid, ok := names[w.Bot]
			if !ok {
				continue
			}
			b := bots[guid]
			a2 := aggs[aggKey{guid, b.Role}]
			if flagRules[guid] == nil {
				flagRules[guid] = map[string]bool{}
			}
			if flagRules[guid][w.Rule] {
				continue // one rule counts once per fight
			}
			flagRules[guid][w.Rule] = true
			a2.ruleFights[w.Rule]++
			if a2.ruleLevel[w.Rule] == "" {
				a2.ruleLevel[w.Rule] = w.Level
				a2.ruleText[w.Rule] = w.Text
			} else {
				a2.ruleLevel[w.Rule] = higherLevel(a2.ruleLevel[w.Rule], w.Level)
			}
		}
		for _, b := range f.Bots {
			if !judged[b.GUID] {
				continue
			}
			a := alive[b.GUID]
			main := mainRate(b, a)
			mMain, mRaw, peers := peerMain(b, f, judged, alive)
			need := lim.MinPeers
			if roleKind(b.Role) == "tank" {
				need = lim.MinTankPeers
			}
			if peers < need || mMain <= 0 || !(main < lim.YellowRatio*mMain) {
				continue
			}
			floor := 0
			switch roleKind(b.Role) {
			case "dps":
				floor = floors.Dps
			case "healer":
				floor = floors.Healer
			}
			if roleKind(b.Role) != "tank" && mRaw < float64(floor) {
				continue
			}
			low[b.GUID] = true
		}

		flaggedGuids := map[uint32]bool{}
		for guid := range flagRules {
			flaggedGuids[guid] = true
		}
		for guid := range low {
			flaggedGuids[guid] = true
		}
		for guid := range flaggedGuids {
			b := bots[guid]
			a2 := aggs[aggKey{guid, b.Role}]
			a2.flagged++
			ref := FightRef{
				Key: input.Key, Day: input.Day, Start: f.Start, Step: f.Step, Leg: f.Leg,
				BossName: f.BossName, Outcome: f.Outcome, Low: low[guid],
			}
			for rule := range flagRules[guid] {
				ref.Rules = append(ref.Rules, rule)
			}
			sort.Strings(ref.Rules)
			a2.refs = append(a2.refs, ref)
			if diedBeforeHalf(b, f) {
				a2.earlyDeaths++
			}
		}
	}

	report := Report{
		Commander: commander, Since: since, Until: until, Run: "all",
		Fights: len(in), Judged: judgedFights, Truncated: truncated,
		Runs: Runs(in, lim.RunGapS), Limits: lim,
		Bots: []BotRow{}, OK: []OKRow{},
	}

	for _, a := range aggs {
		row := a.row(lim)
		if len(row.Reasons) == 0 {
			report.OK = append(report.OK, OKRow{Name: a.name, GUID: a.key.guid, Class: a.class, Role: a.role, Judged: a.judged})
			continue
		}
		report.Bots = append(report.Bots, row)
	}
	sort.Slice(report.Bots, func(i, j int) bool {
		if li, lj := levelRank(report.Bots[i].Level), levelRank(report.Bots[j].Level); li != lj {
			return li < lj
		}
		if report.Bots[i].Flagged != report.Bots[j].Flagged {
			return report.Bots[i].Flagged > report.Bots[j].Flagged
		}
		return report.Bots[i].Name < report.Bots[j].Name
	})
	sort.Slice(report.OK, func(i, j int) bool { return report.OK[i].Name < report.OK[j].Name })
	return report
}

// peerMain is the peers' median main rate and their median raw output, for
// one judged bot. Zero values when the bot has no judged peer.
func peerMain(b combat.FightBot, f combat.Fight, judged map[uint32]bool, alive map[uint32]float64) (float64, float64, int) {
	var rate, raw []float64
	for _, p := range f.Bots {
		if p.GUID == b.GUID || p.Role != b.Role || !judged[p.GUID] {
			continue
		}
		rate = append(rate, mainRate(p, alive[p.GUID]))
		raw = append(raw, mainRaw(p))
	}
	return median(rate), median(raw), len(rate)
}

func diedBeforeHalf(b combat.FightBot, f combat.Fight) bool {
	if b.Deaths == 0 {
		return false
	}
	for _, d := range f.Deaths {
		if d.Name == b.Name {
			return uint64(d.TMs)*2 < uint64(f.DurMS)
		}
	}
	return false
}

// row turns one folded bot into its answer row, reasons and causes included.
func (a *agg) row(lim Limits) BotRow {
	row := BotRow{
		Name: a.name, GUID: a.key.guid, Class: a.class, Role: a.role,
		Fights: a.fights, Judged: a.judged, PeerFights: a.peerFights,
		Flagged: a.flagged, Main: mainName(a.role),
		Reasons: []Reason{}, Causes: []string{}, FightRefs: []FightRef{},
		You:   Rates{Deaths: a.deaths},
		Peers: Peers{},
	}
	if a.judged > 0 && a.alive > 0 {
		row.You.Dps = num(float64(a.dmg)/a.alive, 1)
		row.You.Hps = num(float64(a.heal)/a.alive, 1)
		row.You.Hcps = num(float64(a.hc)/a.alive, 2)
		row.You.Cps = num(float64(a.casts)/a.alive, 2)
	}
	if a.skillAlive > 0 {
		row.You.Skill10 = num(10*float64(a.skillCasts)/a.skillAlive, 2)
	}
	if a.peerFights > 0 && a.peerAlive > 0 {
		row.Peers.Dps = num(a.peersDpsExp/a.peerAlive, 1)
		row.Peers.Hps = num(a.peersHpsExp/a.peerAlive, 1)
		row.Peers.Hcps = num(a.peersHcExp/a.peerAlive, 2)
		row.Peers.Cps = num(a.peersCpsExp/a.peerAlive, 2)
		row.Peers.Deaths = num(a.peerDeathsExp, 1)
	}
	if a.skillPeerAlive > 0 {
		row.Peers.Skill10 = num(a.skillPeerExp/a.skillPeerAlive, 2)
	}
	if a.peerMainExp > 0 {
		row.Ratio.Main = num(a.peerMainNum/a.peerMainExp, 2)
	}
	if a.peerCpsExp > 0 {
		row.Ratio.Cps = num(a.peerCasts/a.peerCpsExp, 2)
	}

	for id := range a.ruleFights {
		text := a.ruleText[id]
		if text == "" {
			text = id
		}
		row.Reasons = append(row.Reasons, a.reasonRule(id, text+" ("+plural(a.ruleFights[id])+")"))
	}
	if row.Ratio.Main != nil && a.peerFights >= lim.MinFights && *row.Ratio.Main < lim.YellowRatio {
		level := "yellow"
		if *row.Ratio.Main < lim.RedRatio {
			level = "red"
		}
		you, peers := 0.0, 0.0
		if row.You.Dps != nil {
			you = *row.You.Dps
		}
		switch row.Main {
		case "hps":
			if row.You.Hps != nil {
				you = *row.You.Hps
			}
			if row.Peers.Hps != nil {
				peers = *row.Peers.Hps
			}
		case "cps":
			if row.You.Cps != nil {
				you = *row.You.Cps
			}
			if row.Peers.Cps != nil {
				peers = *row.Peers.Cps
			}
		default:
			if row.Peers.Dps != nil {
				peers = *row.Peers.Dps
			}
		}
		row.Reasons = append(row.Reasons, Reason{
			ID: "peer:main", Level: level, Fights: a.peerFights,
			Text: fmt1(you) + " " + row.Main + " against a role median of " + fmt1(peers) + " (" +
				itoa(int(math.Round(*row.Ratio.Main*100))) + "%) over " + plural(a.peerFights),
		})
	}
	if row.Main != "cps" && row.Ratio.Cps != nil && a.peerFights >= lim.MinFights && *row.Ratio.Cps < lim.IdleRatio {
		you, peers := 0.0, 0.0
		if row.You.Cps != nil {
			you = *row.You.Cps
		}
		if row.Peers.Cps != nil {
			peers = *row.Peers.Cps
		}
		row.Reasons = append(row.Reasons, Reason{
			ID: "peer:cps", Level: "yellow", Fights: a.peerFights,
			Text: "cast " + fmt2(you) + "/s against a role median of " + fmt2(peers) + "/s over " + plural(a.peerFights),
		})
	}
	if a.peerFights >= lim.MinFights && a.deaths >= lim.MinDeaths &&
		(a.peerDeathsExp == 0 || float64(a.deaths)/a.peerDeathsExp >= lim.DeathRatio) {
		exp := "0"
		if a.peerDeathsExp > 0 {
			exp = fmt1(a.peerDeathsExp)
		}
		row.Reasons = append(row.Reasons, Reason{
			ID: "deaths", Level: "yellow", Fights: a.peerFights,
			Text: "died " + itoa(a.deaths) + " times against " + exp + " expected over " + plural(a.peerFights),
		})
	}

	row.Level = "yellow"
	for _, r := range row.Reasons {
		if r.Level == "red" {
			row.Level = "red"
		}
	}

	if a.didNone > 0 {
		row.Causes = append(row.Causes, "did nothing at all (no cast, hit or heal) in "+plural(a.didNone)+
			": no target, out of range or stuck; the fight data cannot tell which")
	}
	if a.flagged > 0 && float64(a.earlyDeaths)/float64(a.flagged) >= lim.EarlyDeathShare && a.earlyDeaths > 0 {
		row.Causes = append(row.Causes, "died before half the fight in "+itoa(a.earlyDeaths)+" of "+itoa(a.flagged)+" flagged fights")
	}
	if has := hasReason(row.Reasons, "peer:cps"); has {
		you, peers := 0.0, 0.0
		if row.You.Cps != nil {
			you = *row.You.Cps
		}
		if row.Peers.Cps != nil {
			peers = *row.Peers.Cps
		}
		row.Causes = append(row.Causes, "cast "+fmt2(you)+"/s against a role median of "+fmt2(peers)+"/s")
	}

	// Rule reasons first (a fired rule is the strongest evidence), then red
	// before yellow, then by id.
	sort.SliceStable(row.Reasons, func(i, j int) bool {
		ri := strings.HasPrefix(row.Reasons[i].ID, "rule:")
		rj := strings.HasPrefix(row.Reasons[j].ID, "rule:")
		if ri != rj {
			return ri
		}
		if li, lj := levelRank(row.Reasons[i].Level), levelRank(row.Reasons[j].Level); li != lj {
			return li < lj
		}
		return row.Reasons[i].ID < row.Reasons[j].ID
	})

	refs := a.refs
	if len(refs) > lim.MaxFightRefs {
		refs = refs[len(refs)-lim.MaxFightRefs:]
	}
	for i := len(refs) - 1; i >= 0; i-- {
		row.FightRefs = append(row.FightRefs, refs[i])
	}
	return row
}

func hasReason(reasons []Reason, id string) bool {
	for _, r := range reasons {
		if r.ID == id {
			return true
		}
	}
	return false
}

// Runs splits fights in start order into runs: a new run starts when the
// session differs or the gap from the previous fight's end passes gapS.
func Runs(fights []Input, gapS int64) []Run {
	in := append([]Input(nil), fights...)
	sort.SliceStable(in, func(i, j int) bool {
		if in[i].Fight.Start != in[j].Fight.Start {
			return in[i].Fight.Start < in[j].Fight.Start
		}
		return in[i].Key < in[j].Key
	})
	out := []Run{}
	var prevEnd int64
	for _, f := range in {
		end := f.Fight.Start + int64((f.Fight.DurMS+999)/1000)
		if len(out) == 0 || f.Session != out[len(out)-1].Session || f.Fight.Start-prevEnd > gapS {
			out = append(out, Run{Since: f.Fight.Start, Until: end + 1, Fights: 0, Plan: "", Legs: []string{}, Session: f.Session})
		}
		r := &out[len(out)-1]
		r.Fights++
		if end+1 > r.Until {
			r.Until = end + 1
		}
		if r.Plan == "" && f.Fight.Plan != "" {
			r.Plan = f.Fight.Plan
		}
		if f.Fight.Leg != "" {
			seen := false
			for _, leg := range r.Legs {
				if leg == f.Fight.Leg {
					seen = true
					break
				}
			}
			if !seen {
				r.Legs = append(r.Legs, f.Fight.Leg)
			}
		}
		prevEnd = end
	}
	return out
}
