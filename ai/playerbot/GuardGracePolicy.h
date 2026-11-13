#pragma once

// A unit a tank row assigns to a tank slot is left to that tank for this long from the tick the unit first went loose
// (the until_tank window's own cast and walk, a tank_post waiting for the unit to land). Past it the unit is the
// guard's like any loose add: a tank that lost it, never came, or cannot pick it up (taunt-immune, parked on a breath
// seat, stuck) must not leave a healer unprotected. A unit already on its tank is the tank's through
// UnitFact::victimTank, not through this clock.
//
// Shared by the run leader's guard (runtime/DungeonRunPolicy.h) and the boss table's tank_post reservation
// (BossTacticsPolicy.h): the post keeps its subject through the same loose window before another tank may taunt it.

#include <cstdint>

namespace ai
{
    constexpr std::uint32_t kGuardAssignedGraceMs = 8000;
    // AiPlayerbot.DungeonRunGuardClockHittable: a tank_post unit's grace under the key is the post's own
    // (kTankPostReserveGraceMs, pinned equal by the static_assert beside it in BossTacticsPolicy.h).
    constexpr std::uint32_t kGuardPostGraceMs = 15000;
    struct GuardLooseClock
    {
        std::uint64_t looseSinceMs = 0;
        std::uint64_t unhittableSinceMs = 0;
    };
    // One loose tick (the unit's victim is no tank). 0 = not started. The clock starts on the first hittable tick
    // (attackable and selectable: walk 2d, Onyxia's Cindarion hit healers from the air in P1 and the 8 s were spent
    // before he landed); a running clock keeps its start through an unhittable spell shorter than
    // kGuardAssignedGraceMs, a longer one clears it.
    inline GuardLooseClock GuardAssignedLooseClock(std::uint64_t prior, std::uint64_t unhittableSince, bool hittable,
                                                   std::uint64_t nowMs)
    {
        GuardLooseClock out;
        if (hittable)
        {
            out.looseSinceMs = prior != 0 ? prior : nowMs;
            return out;
        }
        out.unhittableSinceMs = unhittableSince != 0 ? unhittableSince : nowMs;
        out.looseSinceMs = nowMs - out.unhittableSinceMs >= kGuardAssignedGraceMs ? 0 : prior;
        return out;
    }
    inline std::uint32_t GuardAssignedGraceFor(bool keyOn, bool fromTankPost)
    {
        return keyOn && fromTankPost ? kGuardPostGraceMs : kGuardAssignedGraceMs;
    }
    // Whether the guard leaves the unit to its slot's tank. Key off with no clock: today's arithmetic (looseSince 0).
    inline bool GuardLeavesToAssignedTank(bool keyOn, bool tankHolds, bool clockStarted, std::uint64_t clockMs,
                                          std::uint64_t looseSince, std::uint32_t graceMs)
    {
        if (!tankHolds)
            return false;
        if (!clockStarted)
            return keyOn || clockMs < graceMs;
        return clockMs - looseSince < graceMs;
    }
}
