#pragma once

#include <cstddef>
#include <cstdint>

// Warlock raid rotation (AiPlayerbot.WarlockRaidRotation, off by default).
// Every rule asks one question: is this row spending a global cooldown and
// mana on a target that is about to die?
//   - a DoT or curse starts only on a target at 35% health or more, a spread
//     to another attacker only while that attacker is above 50%, and a raid
//     group keeps at most two extra dotted targets;
//   - Drain Soul is a trash finisher: never on a boss, and only below two
//     shards, so it never channels a boss execute phase instead of nuking.
// Pure functions of already-read facts, unit-tested on their own
// (tools/test_warlock_raid_rotation_policy.cpp). Key off returns the
// behaviour the rows had before the key existed.

namespace TortoiseBots
{

inline constexpr std::uint8_t WARLOCK_DOT_MIN_TARGET_HEALTH = 35;
inline constexpr std::uint8_t WARLOCK_DOT_MIN_SPREAD_HEALTH = 50;
inline constexpr std::size_t WARLOCK_SPREAD_MAX_EXTRA_TARGETS = 2;
inline constexpr std::uint32_t WARLOCK_DRAIN_SOUL_MAX_SHARDS = 2;  // fewer than this

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

// Drain Soul needs the dying-target envelope (at or below 20% health, bag
// space) either way. With the key it is limited to trash and fewer than two
// shards instead of fewer than five.
inline bool WarlockDrainSoulAllowed(bool raidRotation, bool boss, std::uint32_t shards,
                                    std::uint8_t targetHealthPct, bool hasBagSpace)
{
    if (!hasBagSpace || targetHealthPct > 20)
        return false;
    if (!raidRotation)
        return shards < 5;
    return !boss && shards < WARLOCK_DRAIN_SOUL_MAX_SHARDS;
}

} // namespace TortoiseBots
