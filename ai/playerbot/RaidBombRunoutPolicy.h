#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ai
{
    struct RaidRunoutPoint
    {
        float x;
        float y;
    };

    inline float RaidRunoutDistanceSquared(RaidRunoutPoint a, RaidRunoutPoint b)
    {
        float const dx = a.x - b.x;
        float const dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    inline float RaidRunoutSegmentDistanceSquared(RaidRunoutPoint a, RaidRunoutPoint b, RaidRunoutPoint point)
    {
        float const length = RaidRunoutDistanceSquared(a, b);
        float const t = length > 0 ? std::max(0.0f, std::min(1.0f,
            ((point.x - a.x) * (b.x - a.x) + (point.y - a.y) * (b.y - a.y)) / length)) : 0.0f;
        return RaidRunoutDistanceSquared({ a.x + t * (b.x - a.x), a.y + t * (b.y - a.y) }, point);
    }

    inline bool RaidBombClearOfGroup(RaidRunoutPoint point,
        std::vector<RaidRunoutPoint> const& members, float clearance)
    {
        for (RaidRunoutPoint member : members)
            if (RaidRunoutDistanceSquared(point, member) < clearance * clearance)
                return false;
        return true;
    }

    // One origin per application, not one origin per reaction. Even if group
    // members follow the carrier, candidate searches never march off-map.
    class RaidBombRunoutPlan
    {
    public:
        bool Begin(std::uint32_t spell, std::int64_t applied, std::uint32_t map,
            std::uint32_t instance, RaidRunoutPoint position)
        {
            if (active && spell == spellId && applied == applyTime && map == mapId && instance == instanceId)
                return false;
            active = true;
            spellId = spell;
            applyTime = applied;
            mapId = map;
            instanceId = instance;
            origin = position;
            return true;
        }

        void Reset() { active = false; }

        RaidRunoutPoint Candidate(float radius, float angle) const
        {
            return { origin.x + radius * std::cos(angle), origin.y + radius * std::sin(angle) };
        }

        bool Contains(RaidRunoutPoint point, float limit) const
        {
            return active && RaidRunoutDistanceSquared(origin, point) <= limit * limit;
        }

    private:
        bool active = false;
        std::uint32_t spellId = 0;
        std::int64_t applyTime = 0;
        std::uint32_t mapId = 0;
        std::uint32_t instanceId = 0;
        RaidRunoutPoint origin = { 0, 0 };
    };
}
