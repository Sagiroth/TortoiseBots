#pragma once

#include <cstdint>

// Warlock raid rotation (AiPlayerbot.WarlockRaidRotation, off by default).
// Every rule asks one question: is this row spending a global cooldown and
// mana on a target that is about to die?
//   - a DoT or curse starts only on a target at 35% health or more, and a
//     spread to another attacker only while that attacker is above 50%.
// Pure functions of already-read facts, unit-tested on their own
// (tools/test_warlock_raid_rotation_policy.cpp). Key off returns the
// behaviour the rows had before the key existed.

namespace TortoiseBots
{

inline constexpr std::uint8_t WARLOCK_DOT_MIN_TARGET_HEALTH = 35;
inline constexpr std::uint8_t WARLOCK_DOT_MIN_SPREAD_HEALTH = 50;

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

} // namespace TortoiseBots
