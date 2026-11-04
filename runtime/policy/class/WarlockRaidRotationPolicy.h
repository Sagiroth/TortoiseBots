#pragma once

#include <cstddef>
#include <cstdint>

// Warlock raid rotation (AiPlayerbot.WarlockRaidRotation, off by default).
// Every rule asks one question: is this row spending a global cooldown and
// mana on a target that is about to die?
//   - a DoT or curse starts only on a target at 35% health or more, a spread
//     to another attacker only while that attacker is above 50%, and a raid
//     group keeps at most two extra dotted targets.
// Pure functions of already-read facts, unit-tested on their own
// (tools/test_warlock_raid_rotation_policy.cpp). Key off returns the
// behaviour the rows had before the key existed.

namespace TortoiseBots
{

inline constexpr std::uint8_t WARLOCK_DOT_MIN_TARGET_HEALTH = 35;
inline constexpr std::uint8_t WARLOCK_DOT_MIN_SPREAD_HEALTH = 50;
inline constexpr std::size_t WARLOCK_SPREAD_MAX_EXTRA_TARGETS = 2;

// Current-target rows use the 35% line; "on attacker" spreads need the target
// above 50%.
inline bool WarlockDotAllowed(bool raidRotation, std::uint8_t targetHealthPct, bool spread)
{
    if (!raidRotation)
        return true;
    if (spread)
        return targetHealthPct > WARLOCK_DOT_MIN_SPREAD_HEALTH;
    return targetHealthPct >= WARLOCK_DOT_MIN_TARGET_HEALTH;
}

// How many extra attackers a spread may keep dotted; the current target does
// not count (it is the main DoT's target).
inline bool WarlockSpreadAllowed(bool raidRotation, bool inRaidGroup, std::size_t extraTargetsDotted)
{
    if (!raidRotation || !inRaidGroup)
        return true;
    return extraTargetsDotted < WARLOCK_SPREAD_MAX_EXTRA_TARGETS;
}

} // namespace TortoiseBots
