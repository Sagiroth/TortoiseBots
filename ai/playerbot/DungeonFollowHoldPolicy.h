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
    // The leader must be more than this far away before the bot switches from
    // the core follow to the pack-aware walk. Closer than this the bot is
    // following the leader's own footsteps and the core follow is fine.
    constexpr float kDungeonFollowHoldLeaderDistanceYd = 30.0f;

    // Once the walk is on, it stays on until the leader is closer than this
    // (hysteresis): a leader standing near 30 yd must not flip the bot
    // between the core follow and the hold every second.
    constexpr float kDungeonFollowHoldReleaseDistanceYd = 25.0f;

    // Stop this far outside the creature's aggro radius, so a small step,
    // a waypoint rounding or the pack's idle shuffle does not pull it.
    constexpr float kDungeonFollowHoldMarginYd = 3.0f;

    // Core CREATURE_Z_ATTACK_RANGE: a creature that cannot fly does not
    // aggro on a unit more than this far above or below it. The core measures
    // it with GetDistanceZ, which subtracts both bounding radii, so the caller
    // adds the creature's and the bot's bounding radius to this.
    constexpr float kDungeonFollowHoldZRangeYd = 3.0f;

    // Re-evaluate the route at least this often while walking or holding,
    // so a pack that comes into sight mid-walk is still respected.
    constexpr unsigned kDungeonFollowHoldRecheckMs = 1000;

    // Minimum seconds between two "pack in the way" messages from one bot.
    constexpr unsigned kDungeonFollowHoldTellCooldownSec = 30;

    // Gate: the pack-aware walk only replaces the core follow in a dungeon,
    // with the option on, out of combat on both sides (a leader in combat
    // needs help, not a careful approach) and with the leader far ahead:
    // more than 30 yd to switch it on, more than 25 yd to keep it on.
    inline bool UseDungeonFollowHold(bool enabled, bool inDungeon, bool botInCombat,
        bool leaderInCombat, float leaderDistance, bool alreadyOn = false)
    {
        if (!enabled || !inDungeon || botInCombat || leaderInCombat)
            return false;
        return leaderDistance > (alreadyOn ? kDungeonFollowHoldReleaseDistanceYd : kDungeonFollowHoldLeaderDistanceYd);
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

    // Earliest parameter t in [0, 1] where the segment a->b is inside the
    // creature's aggro zone: within `radius` (3D) of centre c AND within
    // `zRange` vertically of c (zRange <= 0: no vertical limit). The vertical
    // test is made where the leg actually is, so a ramp or stair leg that
    // climbs through the pack's level counts even if both its ends are far
    // above and below. Returns false when the leg never enters the zone.
    // A leg that starts inside the zone counts (t = 0) only while it heads
    // further in: a bot already standing at its hold point stays put, but one
    // whose route leads away from the pack is free to walk off.
    inline bool FollowHoldSegmentEntry(const FollowHoldPoint& a, const FollowHoldPoint& b,
        float cx, float cy, float cz, float radius, float zRange, float& t)
    {
        const float fx = a.x - cx, fy = a.y - cy, fz = a.z - cz;
        const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        const float qa = dx * dx + dy * dy + dz * dz;
        if (qa <= 0.0f || radius <= 0.0f)
            return false; // zero-length segment: no step to take

        const float c = fx * fx + fy * fy + fz * fz - radius * radius;
        const float qb = 2.0f * (fx * dx + fy * dy + fz * dz);
        const float disc = qb * qb - 4.0f * qa * c;
        if (disc < 0.0f)
            return false;

        // Part of the leg inside the sphere: [t0, t1] clipped to [0, 1].
        const float root = std::sqrt(disc);
        float t0 = (-qb - root) / (2.0f * qa);
        float t1 = (-qb + root) / (2.0f * qa);
        if (t1 < 0.0f || t0 > 1.0f)
            return false;
        if (t0 < 0.0f)
            t0 = 0.0f;
        if (t1 > 1.0f)
            t1 = 1.0f;

        // Part of the leg inside the vertical band: [s0, s1].
        float s0 = 0.0f, s1 = 1.0f;
        if (zRange > 0.0f)
        {
            if (std::fabs(dz) < 1e-4f)
            {
                if (std::fabs(fz) > zRange)
                    return false; // level leg on another floor
            }
            else
            {
                float lo = (-zRange - fz) / dz;
                float hi = (zRange - fz) / dz;
                if (lo > hi)
                {
                    const float tmp = lo;
                    lo = hi;
                    hi = tmp;
                }
                s0 = lo > 0.0f ? lo : 0.0f;
                s1 = hi < 1.0f ? hi : 1.0f;
            }
        }

        const float enter = t0 > s0 ? t0 : s0;
        const float leave = t1 < s1 ? t1 : s1;
        if (enter > leave)
            return false;

        // Starts inside the zone but moves away from the creature.
        if (enter <= 0.0f && c <= 0.0f && qb >= 0.0f)
            return false;

        t = enter;
        return true;
    }

    // Walks the route (route[0] = where the bot stands) and returns the first
    // point that enters any pack's aggro zone, with (pack radius + margin) as
    // the sphere: the bot stops `margin` yards outside the aggro radius. A
    // route of fewer than two points has nowhere to go and never holds.
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

                float t = 0.0f;
                if (FollowHoldSegmentEntry(a, b, pack.x, pack.y, pack.z, pack.radius + margin, pack.zRange, t) &&
                    t < bestT)
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
