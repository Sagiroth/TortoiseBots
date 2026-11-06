#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Same-directory include: resolves for the module (ai/ on the path) and
// for tools/test_warrior_gap_closer_policy.cpp (compiled from the root).
#include "WarriorRageGatePolicy.h"

// Warrior gap closers (AiPlayerbot.WarriorGapCloser).
//
// Charge (100 / 6178 / 11578): Battle Stance only, never while the warrior
// is in combat (SPELL_ATTR_CANT_USED_IN_COMBAT), 8-25 yd, no rage cost.
// Intercept (20252 / 20616 / 20617): Berserker Stance only, usable in
// combat, 8-25 yd, 10 rage. Both ranges are measured by the core edge to edge
// (Spell::CheckRange -> GetCombatDistance), so the caller passes that
// distance, not a centre or stance-spot distance.
//
// A stance swap costs no rage itself but keeps only what Tactical Mastery
// lets through (RageAfterStanceSwap), so Intercept from Defensive Stance is
// paid with the post-swap bar. The swap back to Defensive Stance is the tank
// "defensive stance" row's job; the gap-closer rows outrank it so the swap
// and the cast land back to back.

namespace ai
{
    constexpr float WARRIOR_GAP_CLOSER_MIN_RANGE = 8.0f;
    constexpr float WARRIOR_GAP_CLOSER_MAX_RANGE = 25.0f;
    constexpr uint32_t WARRIOR_INTERCEPT_RAGE_COST = 10;

    enum class WarriorGapCloser
    {
        None,
        Charge,
        Intercept
    };

    struct WarriorGapCloserFacts
    {
        bool enabled = true;           // AiPlayerbot.WarriorGapCloser
        bool warriorInCombat = false;  // the warrior itself (Charge refuses then)
        bool isTank = false;
        bool rescue = false;           // the target is a mob hitting a non-tank group member
        float combatDistance = 0.0f;   // core edge-to-edge distance to the target
        bool targetCrowdControlled = false;
        bool targetHeldCcMark = false; // a group CC mark the party leaves alone
        bool lineOfSight = true;
        bool dpsHeld = false;          // the boss-tactics `dps hold` (IsDpsHeld) holds this bot off the target
        bool bossPullbackOrStand = false; // the target is a boss-tactics boss in a pullback or in a stand seat: it is placed, not charged
        bool tankHoldsBoss = false;    // the warrior is the victim of a boss-tactics boss or a rank 3 elite (rescue only)
        bool pullAllowed = true;       // WarriorGapCloserPullAllowed
        bool chargeKnown = false;
        bool chargeReady = false;
        bool interceptKnown = false;
        bool interceptReady = false;
        bool inBerserkerStance = false;
        uint32_t rage = 0;
        unsigned tmRank = 0;
    };

    inline bool WarriorGapCloserInRange(float combatDistance)
    {
        return combatDistance >= WARRIOR_GAP_CLOSER_MIN_RANGE &&
            combatDistance <= WARRIOR_GAP_CLOSER_MAX_RANGE;
    }

    // An engaged mob is fair game. In a dungeon or raid an unengaged one is
    // a pull, and the warrior only opens it when the pull was ordered for
    // that mob as a melee pull (the run's own pulls open from range and a
    // ranged order wants the shot). In the open world the warrior's own
    // target is its own business.
    inline bool WarriorGapCloserPullAllowed(bool targetInCombat, bool inInstance, bool orderedMeleePullOfTarget)
    {
        return targetInCombat || !inInstance || orderedMeleePullOfTarget;
    }

    // Intercept from the current stance: the post-swap bar must pay its cost.
    inline bool WarriorCanAffordIntercept(bool inBerserkerStance, uint32_t rage, unsigned tmRank)
    {
        return RageAfterStanceSwap(inBerserkerStance, rage, tmRank) >= WARRIOR_INTERCEPT_RAGE_COST;
    }

    inline WarriorGapCloser ChooseWarriorGapCloser(WarriorGapCloserFacts const& f)
    {
        if (!f.enabled)
            return WarriorGapCloser::None;
        if (f.targetCrowdControlled || f.targetHeldCcMark)
            return WarriorGapCloser::None;
        // The boss tactics' dps hold wins over a Charge into the held target.
        if (f.dpsHeld)
            return WarriorGapCloser::None;
        // A boss the tactics pull back or seat the raid around is placed, not charged.
        if (f.bossPullbackOrStand)
            return WarriorGapCloser::None;
        if (!f.lineOfSight || !WarriorGapCloserInRange(f.combatDistance))
            return WarriorGapCloser::None;

        if (!f.warriorInCombat)
        {
            if (f.chargeKnown && f.chargeReady && f.pullAllowed)
                return WarriorGapCloser::Charge;
            return WarriorGapCloser::None;
        }

        // In combat only a tank closes in, and only to take a mob off a
        // non-tank member; the rest of the fight walks. A tank holding a boss
        // stays in Defensive Stance on it: only a tank without one rescues.
        if (f.isTank && f.rescue && !f.tankHoldsBoss && f.interceptKnown && f.interceptReady &&
            WarriorCanAffordIntercept(f.inBerserkerStance, f.rage, f.tmRank))
            return WarriorGapCloser::Intercept;
        return WarriorGapCloser::None;
    }

    // The first candidate (in the caller's priority order: the mob on a
    // non-tank member, then the warrior's own target) that yields a gap
    // closer. Returns candidates.size() when none does.
    inline std::size_t PickWarriorGapCloserTarget(std::vector<WarriorGapCloserFacts> const& candidates)
    {
        for (std::size_t i = 0; i < candidates.size(); ++i)
            if (ChooseWarriorGapCloser(candidates[i]) != WarriorGapCloser::None)
                return i;
        return candidates.size();
    }
} // namespace ai
