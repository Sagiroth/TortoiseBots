// Package combat stores the module's COMBAT_FIGHT datagrams and judges them.
//
// The module (runtime/CombatMonitorPolicy.h, task A8a) aggregates one fight
// per bound group: casts, damage, healing, deaths and debuff uptime per bot.
// This package keeps the last fights per commander and runs the v1 rule table
// (rules.json) over them. Numbers only cross the wire; the warnings are made
// here.
package combat

// SpellCount is one spell line of a bot: name and casts.
type SpellCount struct {
	N string `json:"n"`
	C uint32 `json:"c"`
}

// FightBot is one bot's numbers in a fight. Role is "tank", "healer" or
// "dps"; Sub is the raid subgroup (or -1 outside a raid).
type FightBot struct {
	Name      string       `json:"name"`
	GUID      uint32       `json:"guid"`
	Class     string       `json:"class"`
	Role      string       `json:"role"`
	Sub       int          `json:"sub"`
	Dmg       uint64       `json:"dmg"`
	Heal      uint64       `json:"heal"`
	Over      uint64       `json:"over"`
	Taken     uint64       `json:"taken"`
	Casts     uint32       `json:"casts"`
	HealCasts uint32       `json:"heal_casts"`
	Deaths    uint32       `json:"deaths"`
	Spells    []SpellCount `json:"spells"`
}

// EngagedEntry is one enemy entry the members fought: entry, name and how
// many distinct units of it were engaged.
type EngagedEntry struct {
	E uint32 `json:"e"`
	N string `json:"n"`
	C uint32 `json:"c"`
}

// FightDebuffs is the 1 Hz aura sample: seconds sampled and seconds each
// tracked debuff key was up on the focus unit.
type FightDebuffs struct {
	SampledS uint64            `json:"sampled_s"`
	UpS      map[string]uint64 `json:"up_s"`
}

// FightHit is one hit of a death recap: attacker name, amount, ms since
// fight start.
type FightHit struct {
	A   string `json:"a"`
	D   uint32 `json:"d"`
	TMs uint32 `json:"t_ms"`
}

// FightDeath is one bot death: who, when, where, by what, and the last hits
// taken, newest last.
type FightDeath struct {
	Name   string     `json:"name"`
	TMs    uint32     `json:"t_ms"`
	X      float64    `json:"x"`
	Y      float64    `json:"y"`
	Z      float64    `json:"z"`
	Killer string     `json:"killer"`
	Hits   []FightHit `json:"hits"`
}

// Fight is one finished fight of a bound group, mirrored from the module's
// COMBAT_FIGHT "fight" object (keys identical so the module's own test and
// this package read the same bytes).
type Fight struct {
	ID            string         `json:"id"`
	Commander     string         `json:"commander"`
	CommanderGUID uint32         `json:"commander_guid"`
	Group         uint32         `json:"group"`
	Map           uint32         `json:"map"`
	Plan          string         `json:"plan"`
	Leg           string         `json:"leg"`
	Step          uint32         `json:"step"`
	Kind          string         `json:"kind"`
	Start         int64          `json:"start"`
	DurMS         uint32         `json:"dur_ms"`
	Outcome       string         `json:"outcome"`
	Boss          uint32         `json:"boss"`
	BossName      string         `json:"boss_name"`
	Planned       []uint32       `json:"planned"`
	Engaged       []EngagedEntry `json:"engaged"`
	Bots          []FightBot     `json:"bots"`
	Debuffs       FightDebuffs   `json:"debuffs"`
	Deaths        []FightDeath   `json:"deaths"`
}

// FightDatagram is the COMBAT_FIGHT datagram envelope. The module sends each
// finished fight twice with the same id (UDP loss); the store dedupes.
type FightDatagram struct {
	V       int    `json:"v"`
	Session uint64 `json:"session"`
	Seq     uint64 `json:"seq"`
	TS      int64  `json:"ts"`
	Type    string `json:"type"`
	Fight   Fight  `json:"fight"`
}

// FightSummary is one row of GET /api/v1/combat/fights.
type FightSummary struct {
	ID       string `json:"id"`
	Key      string `json:"key"`
	Session  uint64 `json:"session"`
	Start    int64  `json:"start"`
	Dur      uint32 `json:"dur"`
	Outcome  string `json:"outcome"`
	BossName string `json:"boss_name"`
	Leg      string `json:"leg"`
	Step     uint32 `json:"step"`
	Deaths   int    `json:"deaths"`
	Warnings int    `json:"warnings"`
	// Live marks the row of a fight still running (COMBAT_EVENTS opened it and
	// no COMBAT_FIGHT has arrived yet): it has no per-bot numbers and no
	// detail to load, and the card shows it as it happens.
	Live bool `json:"live,omitempty"`
}

// Warning is one fired rule on one bot.
type Warning struct {
	Rule  string `json:"rule"`
	Level string `json:"level"` // red | yellow | info
	Bot   string `json:"bot"`
	Role  string `json:"role"`
	Text  string `json:"text"`
}

// Rule is one row of rules.json.
type Rule struct {
	ID        string `json:"id"`
	Level     string `json:"level"`
	Role      string `json:"role"`
	Class     string `json:"class"`
	Text      string `json:"text"`
	MinFightS int    `json:"min_fight_s"`
	Condition string `json:"condition"`
}
