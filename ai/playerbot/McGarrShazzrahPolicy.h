#pragma once

#include <cstdint>
#include <string>

// Pure decision rules for the cheap MC per-boss bundle (mod-playerbots
// parity, raid1 batch item 3): Garr AoE-off + Shazzrah 26y range.
// Donor: mod-playerbots @ 79bd4281, src/Ai/Raid/MC/MCMultipliers.cpp:26-53
// (GarrDisableDpsAoeMultiplier: DpsAoeAction + a named AoE-spell list +
// any ACTION_THREAT_AOE cast while Garr lives) + MCTriggers.cpp:27-37
// (McShazzrahRangedTrigger: ranged inside ARCANE_EXPLOSION_DISTANCE) +
// MCActions.cpp:51-60 (step out to exactly 26y) + MCHelpers.h:38 (26y).
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_mc_garr_shazzrah_policy.cpp.

namespace ai
{
    // Garr 12057, Shazzrah 12264 (1.18.1 verified).
    constexpr std::uint32_t kGarrEntry = 12057;
    constexpr std::uint32_t kShazzrahEntry = 12264;

    // Donor ARCANE_EXPLOSION_DISTANCE: ranged steps out to exactly 26y.
    constexpr float kShazzrahRangeDistance = 26.0f;

    // Garr: DPS-bot AoE suppressed while Garr lives. The donor names its
    // AoE set by action type; here the caller passes the already-computed
    // verdict (is this bot DPSing, is this action an AoE) so the policy
    // stays free of Action-type coupling.
    inline bool ShouldSuppressGarrAoe(bool garrAlive, bool botIsDps, bool actionIsAoe)
    {
        return garrAlive && botIsDps && actionIsAoe;
    }

    // Shazzrah: ranged bots inside 26y step out; everyone else holds.
    // (The donor gates on IsRanged only, not healer — mirror that.)
    inline bool ShouldLeaveShazzrahRange(bool botIsRanged, bool insideRange)
    {
        return botIsRanged && insideRange;
    }

    inline bool IsShazzrahMoveAction(const std::string& actionName)
    {
        return actionName == "move away from shazzrah";
    }
}
