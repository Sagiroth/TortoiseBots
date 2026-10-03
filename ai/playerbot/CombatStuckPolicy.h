#pragma once

#include <cstdint>
#include <string>

// Pure policy for the combat-stuck give-up (m-stuck section 5.3, finding 5):
// the combat-stuck handler only reset, leaving the attacker, so the bot
// re-engaged the same mob until a long-stuck rescue fired - 2,670
// combat-stuck trips (78.9% of stuck episodes) against 371 ReachGiveUps.
// Mirror ReachTargetAction's give-up instead: blacklist the combat target
// (guid + creature kind) for five minutes and drop the order with it, so
// target selection steers elsewhere. AttackersValue::IgnoreTarget still lets
// a mob that is genuinely fighting the bot (victim, line of sight,
// reachable) back in, so giving up is safe mid-fight. The world-facing parts
// (AI values, AttackStop, logEvent) live in UnstuckAction; the decisions
// below are pure so they can be tested on their own.

namespace ai
{
    // Reach give-up window (ReachTargetActions.h): a wedged mob is left alone
    // this long, then becomes eligible again.
    constexpr std::uint32_t COMBAT_STUCK_BLACKLIST_MS = 5 * 60 * 1000;

    inline std::uint32_t CombatStuckBlacklistExpiry(std::uint32_t nowMs)
    {
        return nowMs + COMBAT_STUCK_BLACKLIST_MS;
    }

    // Give up only on something real: no target means nothing to blacklist, a
    // corpse needs looting (SelectNewTargetAction saves it from the order we
    // would clear), and the bot itself must never be blacklisted. A mob that is
    // hitting the bot is a fight, not a wedge: dropping it left the bot taking
    // hits without answering (live 2026-10-03: 19% of deaths within a minute of
    // such a give-up), so only a target that is not attacking the bot is dropped.
    inline bool ShouldGiveUpCombatStuckTarget(bool hasTarget, bool targetAlive, bool targetIsSelf, bool targetAttacksBot)
    {
        return hasTarget && targetAlive && !targetIsSelf && !targetAttacksBot;
    }

    // Only the one wedged mob is dropped, never its whole kind: an entry
    // blacklist also drops the grind destination (GrindTravelDestination::
    // IsActive), and common starter mobs (Mangy Wolf, Plainstrider) got
    // blacklisted pool-wide within minutes, pushing bots onto harder mobs.
    inline bool ShouldBlacklistCombatStuckEntry(bool targetIsCreature)
    {
        (void)targetIsCreature;
        return false;
    }

    // Logged under ReachGiveUp so the give-up stays in the one funnel the
    // dashboard and m-stuck queries already read, separable by reason.
    inline std::string CombatStuckGiveUpReason()
    {
        return "combat-stuck";
    }
}
