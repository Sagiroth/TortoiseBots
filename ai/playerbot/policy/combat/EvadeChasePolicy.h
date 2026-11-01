#pragma once

// Pure rules for R2 (fair-fight losses): do not chase a mob that is evading
// home, and drop an evading target dragged past the leash instead of
// running after it into its camp. A mob the core merely flags unreachable
// is still damaging: it keeps the order and falls through to the movement
// and the 15 s give-up, which already treats it as cannot-be-reached.
//
// Live pool (EvadeProbe detail rows, 00:00-06:00): evade=1 in 69% of
// samples, 638 rows with the bot IN combat against an evading mob that
// holds it as victim, homedist p90 99 yd / csdist p90 92 yd. While a
// creature evades it is invulnerable and regenerating, and the chase
// drags the fight back into the camp that spawned it (adds: 49% of fair
// deaths list adds, killer at 89% HP). The 15 s no-headway give-up in
// ReachTargetAction covers wedges but not the per-fight drain: every tick
// spent chasing an evader is mana, GCDs and yards into the next pack.
//
// No core includes: the reach action translates creature state
// (IsInEvadeMode / IsEvadeBecauseTargetNotReachable, both already used in
// ReachTargetAction::TargetCannotBeReached) plus the same respawn/combat
// positions the EvadeProbe diagnostic logs (GetRespawnCoord,
// GetCombatStartPosition) into these plain inputs, so the rules stay
// tested in tools/test_evade_chase_policy.cpp.

#include <cstdint>

namespace ai
{
    // Yards past its home (respawn) point at which a target is "dragged
    // home": past the aggro radius plus wander. Matches the danger radius
    // the elite-camp gate uses (PointDangerPolicy.h), so a fight dragged
    // that far is already inside the camp the gate would have barred.
    constexpr float kEvadeChaseHomeLeashYd = 40.0f;

    // Home distance alone is not evidence: report says WHERE the leash is,
    // the evade flag says the mob is actually going home.
    struct EvadeChaseState
    {
        bool evading = false;       // IsInEvadeMode() — walking home, invulnerable
        bool unreachable = false;   // IsEvadeBecauseTargetNotReachable() — cannot close on the bot
        float homeDistanceYd = 0.0f; // bot-to-target? no: target-to-its-respawn, like the probe's homedist
        float combatStartDistanceYd = 0.0f; // target-to-combat-start, like the probe's csdist
    };

    // Do not start/keep chasing an evading mob: it is walking home
    // invulnerable, and the first step after it drags the fight toward its
    // camp. A mob the core merely flags unreachable is still damaging, so
    // the caller keeps the order and falls through to the movement and the
    // 15 s give-up below (which already treats it as cannot-be-reached);
    // holding the target there left the bot standing in combat without a
    // swing. Evade already feeds TargetCannotBeReached for the give-up.
    inline bool ShouldNotChaseEvader(bool evading)
    {
        return evading;
    }

    // Drop the order: the evading target is already past the home leash (or
    // the fight drifted that far from where it started). Evade-only: a mob
    // the core merely flags unreachable is still damaging, so it keeps the
    // order and the reach falls through to the movement and the 15 s
    // give-up. Dropping lets CombatEnd fire and the non-combat engine
    // re-pick at full resources, instead of standing an evader back to full
    // HP and re-aggroing it next to its camp.
    inline bool ShouldDropDraggedTarget(EvadeChaseState const& state)
    {
        if (!state.evading)
            return false;
        return state.homeDistanceYd > kEvadeChaseHomeLeashYd ||
            state.combatStartDistanceYd > kEvadeChaseHomeLeashYd;
    }
}
