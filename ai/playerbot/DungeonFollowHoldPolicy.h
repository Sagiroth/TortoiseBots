#pragma once

// Pure decision rules for "hold before a pack" dungeon follow
// (AiPlayerbot.DungeonFollowHoldBeforePacks, issue #127).
//
// When the leader is far ahead inside a dungeon and nobody is fighting, a
// following bot walks the navmesh route to the leader with the module's own
// MoveTo instead of the core follow, and stops just outside the aggro radius
// of the first unpulled hostile creature that route passes. Without it the
// bot walks straight past (or through) packs the leader skipped and pulls
// them on its own (Uldaman: the only route north passes ~3 yd from the west
// pack).
//
// Not a mod-playerbots port: the donor follows with the plain core follow
// and has no pack awareness on the way to the leader.
//
// Callers own the game state (route, creature positions, aggro radius from
// Creature::GetAttackDistance, LOS, combat). This header only owns the gate
// and the geometry, so they stay testable in
// tools/test_dungeon_follow_hold_policy.cpp. No core includes.

#include <cmath>
#include <cstddef>

namespace ai
{
    // The leader must be at least this far away before the bot switches from
    // the core follow to the pack-aware walk. Closer than this the bot is
    // following the leader's own footsteps and the core follow is fine.
    constexpr float kDungeonFollowHoldLeaderDistanceYd = 30.0f;

    // Stop this far outside the creature's aggro radius, so a small step,
    // a waypoint rounding or the pack's idle shuffle does not pull it.
    constexpr float kDungeonFollowHoldMarginYd = 3.0f;

    // Core CREATURE_Z_ATTACK_RANGE: a creature that cannot fly does not
    // aggro on a unit more than this far above or below it.
    constexpr float kDungeonFollowHoldZRangeYd = 3.0f;

    // Re-evaluate the route at least this often while walking or holding,
    // so a pack that comes into sight mid-walk is still respected.
    constexpr unsigned kDungeonFollowHoldRecheckMs = 1000;

    // Minimum seconds between two "pack in the way" messages from one bot.
    constexpr unsigned kDungeonFollowHoldTellCooldownSec = 30;

    // Gate: the pack-aware walk only replaces the core follow in a dungeon,
    // with the option on, out of combat on both sides (a leader in combat
    // needs help, not a careful approach) and with the leader far ahead.
    inline bool UseDungeonFollowHold(bool enabled, bool inDungeon, bool botInCombat,
        bool leaderInCombat, float leaderDistance)
    {
        if (!enabled || !inDungeon || botInCombat || leaderInCombat)
            return false;
        return leaderDistance > kDungeonFollowHoldLeaderDistanceYd;
    }

    struct FollowHoldPoint
    {
        float x;
        float y;
        float z;
    };

    // One unpulled hostile creature: centre and aggro radius (yards).
    // zRange <= 0 means no vertical limit (a flying creature).
    struct FollowHoldPack
    {
        float x;
        float y;
        float z;
        float radius;
        float zRange;
    };

    struct FollowHoldResult
    {
        bool hold = false;
        // Index into the route of the segment start: the hold point lies
        // between route[segment] and route[segment + 1].
        std::size_t segment = 0;
        // Index into the pack list of the creature that stops the bot.
        std::size_t pack = 0;
        FollowHoldPoint point = { 0.0f, 0.0f, 0.0f };
    };

    // Earliest parameter t in [0, 1] where the segment a->b comes within
    // `radius` (3D) of centre c. Returns false when it never does.
    // A segment that starts inside the radius counts (t = 0) only while it
    // heads further in: a bot already standing at its hold point stays put,
    // but one whose route leads away from the pack is free to walk off.
    inline bool FollowHoldSegmentEntry(const FollowHoldPoint& a, const FollowHoldPoint& b,
        float cx, float cy, float cz, float radius, float& t)
    {
        const float fx = a.x - cx, fy = a.y - cy, fz = a.z - cz;
        const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        const float qa = dx * dx + dy * dy + dz * dz;
        if (qa <= 0.0f)
            return false; // zero-length segment: no step to take

        const float c = fx * fx + fy * fy + fz * fz - radius * radius;
        const float qb = 2.0f * (fx * dx + fy * dy + fz * dz);
        if (c <= 0.0f)
        {
            if (qb >= 0.0f)
                return false; // starts inside but moves away (or along the edge)
            t = 0.0f;         // starts inside and moves closer
            return true;
        }

        const float disc = qb * qb - 4.0f * qa * c;
        if (disc < 0.0f)
            return false;

        // a is outside (c > 0), so the smaller root is the entry.
        const float entry = (-qb - std::sqrt(disc)) / (2.0f * qa);
        if (entry < 0.0f || entry > 1.0f)
            return false;

        t = entry;
        return true;
    }

    // Walks the route (route[0] = where the bot stands) and returns the first
    // point that comes within (pack radius + margin) of any pack. The hold
    // point is that entry point, so the bot stops `margin` yards outside the
    // aggro radius. A pack only counts on a segment whose either end is
    // within its zRange vertically (another floor does not aggro). A route
    // of fewer than two points has nowhere to go and never holds.
    inline FollowHoldResult FindFollowHoldPoint(const FollowHoldPoint* route, std::size_t routeCount,
        const FollowHoldPack* packs, std::size_t packCount, float margin)
    {
        FollowHoldResult result;
        if (!route || !packs || routeCount < 2 || packCount == 0)
            return result;

        for (std::size_t s = 0; s + 1 < routeCount; ++s)
        {
            const FollowHoldPoint& a = route[s];
            const FollowHoldPoint& b = route[s + 1];

            bool found = false;
            float bestT = 2.0f;
            std::size_t bestPack = 0;
            for (std::size_t p = 0; p < packCount; ++p)
            {
                const FollowHoldPack& pack = packs[p];
                if (pack.radius <= 0.0f)
                    continue;
                if (pack.zRange > 0.0f && std::fabs(a.z - pack.z) > pack.zRange &&
                    std::fabs(b.z - pack.z) > pack.zRange)
                    continue;

                float t = 0.0f;
                if (FollowHoldSegmentEntry(a, b, pack.x, pack.y, pack.z, pack.radius + margin, t) && t < bestT)
                {
                    found = true;
                    bestT = t;
                    bestPack = p;
                }
            }

            if (found)
            {
                result.hold = true;
                result.segment = s;
                result.pack = bestPack;
                result.point.x = a.x + (b.x - a.x) * bestT;
                result.point.y = a.y + (b.y - a.y) * bestT;
                result.point.z = a.z + (b.z - a.z) * bestT;
                return result;
            }
        }

        return result;
    }
} // namespace ai
