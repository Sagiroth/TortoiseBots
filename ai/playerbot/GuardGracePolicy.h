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
}
