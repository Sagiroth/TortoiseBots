// The live fight's gate: a row COMBAT_EVENTS opens is listed only while a hostile unit is engaged with the group.
//
// The module's book starts on any member entering combat and the 1 Hz tick ends it three seconds after the last
// member leaves, but that rule does not separate a fight from a parked raid: buffs, consumables and heals cast on the
// group keep a member "in combat" for minutes and the book stays open (an Onyxia stand at 173 minutes, no damage). The
// commander snapshot the module sends carries what tells the two apart: the mobs near the lead (a hostile one whose
// victim is a group member is engaged) and the live window's per-bot dps (the damage of its 10 s window).
package combat

import (
	"strconv"

	"tortoise-observability/internal/model"
)

// Engaged reports whether a commander binding's snapshot shows a hostile unit engaged with the group: a hostile mob
// whose victim is a group member, or damage the group dealt to a hostile inside the module's 10 s window (a bot's dps
// is that window's damage, so a kill or an evade empties it within 10 s). A binding without a live window is not in a
// fight at all. Buffs, consumables and heals on a parked raid are not engagement.
func Engaged(b model.CommanderBinding) bool {
	if b.Combat == nil {
		return false
	}
	ids := make(map[string]bool, len(b.Bots))
	for _, bot := range b.Bots {
		switch {
		case bot.ID != "":
			ids[bot.ID] = true
		case bot.GUID != 0:
			// A report from before bots carried their raw guid: a player's raw guid is its low part.
			ids[strconv.FormatUint(uint64(bot.GUID), 10)] = true
		}
	}
	for _, m := range b.Mobs {
		if m.Reaction == "h" && m.Victim != "" && ids[m.Victim] {
			return true
		}
	}
	for _, w := range b.Combat.Bots {
		if w.DPS > 0 {
			return true
		}
	}
	return false
}
