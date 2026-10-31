#pragma once

// Raid snare rule: in a raid group a snare is only worth the GCD on a mob
// that is attacking someone who is not a tank. Raid trash on the tank is
// already controlled; snaring it (Piercing Howl, Hamstring) spends the
// warrior's GCDs for nothing. Outside a raid group nothing changes.
//
// "Tank" is whoever the module identifies as a tank (forced role, tank
// strategy, talents: PlayerbotAI::IsTank) or carries the raid's Main Tank
// flag (Group::GetMainTankGuid) — the flag is an explicit order from the
// raid leader and sticks whatever the spec reads.

namespace ai
{
    // inRaidGroup: the bot's group is a raid (Group::isRaidGroup, not the
    // instance map: a 5-man party in a dungeon is not a raid). victimIsTank:
    // the mob's victim is a tank by the read above. fleeing: FLEEING motion
    // picks are never a positioning problem (shared runner-finish rule).
    inline bool SnareAllowedInRaid(bool inRaidGroup, bool victimIsTank, bool fleeing)
    {
        if (fleeing)
            return true;
        if (!inRaidGroup)
            return true;
        return !victimIsTank;
    }

    // Spell 12323 (Piercing Howl) is a 10-yd AoE around the warrior. The
    // "snare target" value returns any snare-worthy mob in spell range (30
    // yd), far outside the Howl: casting at the melee target then spends the
    // rage on a mob that is already controlled and never reaches the unit
    // that armed the row. A unit out of the radius is never a Howl target.
    constexpr float PIERCING_HOWL_RADIUS_YD = 10.0f;

    // hasSnareTarget: the snare value picked a unit at all. snareDistYd: its
    // distance to the warrior (ignored without one). inRaidGroup: the bot's
    // group is a raid. snareFleeing: the unit runs from combat, not at the
    // group. In a raid a mob chasing someone is the off-tank's job; only a
    // runner beside the warrior is worth the Howl. The gate measures only
    // the first snare target the value returns; a second one is not read.
    inline bool PiercingHowlAllowed(bool hasSnareTarget, float snareDistYd, bool inRaidGroup, bool snareFleeing)
    {
        if (!hasSnareTarget)
            return false;
        if (snareDistYd > PIERCING_HOWL_RADIUS_YD)
            return false;
        if (inRaidGroup && !snareFleeing)
            return false;
        return true;
    }
} // namespace ai
