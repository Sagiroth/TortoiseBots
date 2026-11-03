#pragma once

// The enemy-area scan both crowd-control area guards read: the cast-start guard
// (GenericSpellActions.cpp BreaksCcInArea) and the running channel or ground
// effect's cancel (CcGuard::CcInRunningArea). A crowd-controlled unit attacks
// nobody, so it carries no threat on this bot and never enters "attackers":
// a sheep placed before this bot touched the pack was invisible to the start
// guard while the channel check found it, and the area spell was cast,
// cancelled and cast again. Both now read the same "possible targets no los"
// list through this scan.
//
// Plain candidates and callables only: the callers own the game state (unit
// lookup, aliveness, map, distance, the damage-breakable CC test), so
// tools/test_cc_area_guard_policy.cpp compiles the real rule.

#include <cstdint>
#include <list>

namespace ai
{
    // True when a candidate `getUnit` resolves stands alive on `mapId`, within
    // `radius` of (x, y, z), and `breakableCc` judges it damage-breakable crowd
    // control. `distance(unit, x, y, z)` is the 3D distance (production:
    // Unit::GetDistance).
    template <typename Guid, typename GetUnit, typename Alive, typename SameMap, typename Distance,
        typename BreakableCc>
    inline bool AnyBreakableCcInArea(std::list<Guid> const& candidates, GetUnit getUnit, Alive alive,
        SameMap sameMap, Distance distance, BreakableCc breakableCc, std::uint32_t mapId, float x, float y, float z,
        float radius)
    {
        for (Guid const& guid : candidates)
        {
            auto const* const unit = getUnit(guid);
            if (!unit || !alive(unit) || !sameMap(unit, mapId))
                continue;
            if (distance(unit, x, y, z) > radius)
                continue;
            if (breakableCc(unit))
                return true;
        }
        return false;
    }
}
