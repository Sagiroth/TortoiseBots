#pragma once

#include <cstddef>
#include <cstdint>

// Warlock raid rotation (AiPlayerbot.WarlockRaidRotation, off by default).
// Every rule asks one question: is this row spending a global cooldown and
// mana on a target that is about to die?
//   - a DoT or curse starts only on a target at 35% health or more, a spread
//     to another attacker only while that attacker is above 50%, and a raid
//     group keeps at most two extra dotted targets per spread row; bosses and
//     the raid-utility curses are never gated;
//   - Drain Soul is a trash finisher: never on a boss, and only below two
//     shards, so it never channels a boss execute phase instead of nuking;
//   - Life Tap in combat only when two filler casts cannot be paid for.
// Pure functions of already-read facts, unit-tested on their own
// (tools/test_warlock_raid_rotation_policy.cpp). Key off returns the
// behaviour the rows had before the key existed.

namespace TortoiseBots
{

inline constexpr std::uint8_t WARLOCK_DOT_MIN_TARGET_HEALTH = 35;
inline constexpr std::uint8_t WARLOCK_DOT_MIN_SPREAD_HEALTH = 50;
inline constexpr std::size_t WARLOCK_SPREAD_MAX_EXTRA_TARGETS = 2;
inline constexpr std::uint32_t WARLOCK_DRAIN_SOUL_MAX_SHARDS = 2;  // fewer than this
inline constexpr std::uint32_t WARLOCK_COMBAT_TAP_NUKE_COUNT = 2;  // filler casts paid up front

// Any one signal makes the target a boss: a world/raid boss, an
// instance-binding boss, or a boss row of the loaded dungeon plan (5-man
// bosses carry the elite rank).
inline bool WarlockIsBossTarget(bool worldBoss, bool instanceBind, bool dungeonPlanBoss)
{
    return worldBoss || instanceBind || dungeonPlanBoss;
}

// Current-target rows use the 35% line; "on attacker" spreads need the target
// above 50%. A boss keeps its execute phase worth dotting, and a raid-utility
// curse (Elements, Recklessness, Shadow) is worth its slot at any health.
inline bool WarlockDotAllowed(bool raidRotation, std::uint8_t targetHealthPct, bool spread,
                              bool boss = false, bool raidUtility = false)
{
    if (!raidRotation)
        return true;
    if (boss || raidUtility)
        return true;
    if (spread)
        return targetHealthPct > WARLOCK_DOT_MIN_SPREAD_HEALTH;
    return targetHealthPct >= WARLOCK_DOT_MIN_TARGET_HEALTH;
}

// The spread cap is a raid rule: a five-player party keeps every attacker the
// old rotation dotted.
inline bool WarlockSpreadCapApplies(bool raidRotation, bool inRaidGroup)
{
    return raidRotation && inRaidGroup;
}

// How many extra attackers a spread may keep dotted; the current target does
// not count (it is the main DoT's target).
inline bool WarlockSpreadAllowed(bool raidRotation, bool inRaidGroup, std::size_t extraTargetsDotted)
{
    if (!WarlockSpreadCapApplies(raidRotation, inRaidGroup))
        return true;
    return extraTargetsDotted < WARLOCK_SPREAD_MAX_EXTRA_TARGETS;
}

// A spread candidate is legal only while it misses the aura and the life gate
// allows it. `ignoreHealth` is a player's manual curse order: it lifts the
// health line only, never the raid cap (that is checked once per row).
inline bool WarlockSpreadCandidateAllowed(bool raidRotation, std::uint8_t targetHealthPct,
                                          bool boss, bool raidUtility, bool hasAura,
                                          bool ignoreHealth = false)
{
    if (hasAura)
        return false;
    if (ignoreHealth)
        return true;
    return WarlockDotAllowed(raidRotation, targetHealthPct, true, boss, raidUtility);
}

// The first item in [begin, end) accepted by `allowed`, or `end`. A rejected
// item never stops the scan, so a health-gated attacker cannot hide a later
// legal one.
template <typename Iterator, typename Allowed>
inline Iterator WarlockFirstAllowed(Iterator begin, Iterator end, Allowed allowed)
{
    for (; begin != end; ++begin)
        if (allowed(*begin))
            return begin;
    return end;
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

// A filler the warlock has not learned prices at 0: keep the mana-line tap.
inline bool WarlockCombatTapAllowed(bool raidRotation, std::uint32_t mana, std::uint32_t nukeCost)
{
    if (!raidRotation || !nukeCost)
        return true;
    return mana < nukeCost * WARLOCK_COMBAT_TAP_NUKE_COUNT;
}

} // namespace TortoiseBots
