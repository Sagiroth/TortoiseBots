#pragma once

// The boss anchor (`ai_boss_tactic` kind `anchor`, docs/play/boss-tactics.md): while its boss fights, every movement
// destination the boss and raid code picks stays inside a circle around the boss's home. Hakkar's script evades him
// the moment he steps below z 45.8 (boss_hakkar.cpp), and that line runs 45-50 yd SW of his spawn; a raid standing on
// the slope drags him over it on every target swap.
//
// Pure: the glue (ClampMoveToBossAnchor in strategy/actions/BossTacticsActions.cpp) reads the row, the bot's role and
// the floor, and every mover calls it. Tested in tools/test_boss_anchor_policy.cpp.

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace ai
{
    struct BossAnchorCircle
    {
        float x = 0.0f, y = 0.0f, z = 0.0f; // the centre (the boss's home)
        float radius = 0.0f;                // this bot's radius (tanks may have a tighter one)
    };

    // A clamped destination, and a seek-pull Son, must stand within this height of the centre: the anchored floor is
    // the boss's own platform, and a point that projects onto a slope or a lower ledge is no stand.
    constexpr float kBossAnchorFloorYd = 4.0f;
    // A seek-pull Son may stand this far past the puller's radius: the clamped walk then ends within the pull's hit
    // reach of it (SeekAuraPolicy.h kSeekPullHitYd).
    constexpr float kBossAnchorSeekPullMarginYd = 5.0f;
    // A chase may go this far past the circle: a melee bot on the circle still reaches a target standing here.
    constexpr float kBossAnchorChaseReachYd = 5.0f;
    // Rounding slack, so a point clamped onto the circle reads as inside it.
    constexpr float kBossAnchorEpsYd = 0.05f;

    // The bot's radius: the tanks' own when the row has one (non-zero), else the shared one.
    inline float BossAnchorRadius(float radius, float tankRadius, bool tank)
    {
        return tank && tankRadius > 0.0f ? tankRadius : radius;
    }

    inline float BossAnchorDistance2d(const BossAnchorCircle& c, float x, float y)
    {
        float const dx = x - c.x;
        float const dy = y - c.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    inline bool InsideAnchor(const BossAnchorCircle& c, float x, float y)
    {
        return BossAnchorDistance2d(c, x, y) <= c.radius + kBossAnchorEpsYd;
    }

    // A destination outside the circle moves onto it, on the line from the centre toward the destination; true when it
    // moved. The height is the caller's: it reads the floor at the new point (AnchorFloorOk).
    inline bool ClampToAnchor(const BossAnchorCircle& c, float& x, float& y)
    {
        float const d = BossAnchorDistance2d(c, x, y);
        if (d <= c.radius + kBossAnchorEpsYd)
            return false;
        float const scale = c.radius / d;
        x = c.x + (x - c.x) * scale;
        y = c.y + (y - c.y) * scale;
        return true;
    }

    // Whether the floor read at a clamped point keeps the move: found, and on the centre's level. Otherwise the move is
    // skipped this tick rather than sent somewhere off the platform.
    inline bool AnchorFloorOk(const BossAnchorCircle& c, bool found, float floorZ)
    {
        return found && std::fabs(floorZ - c.z) <= kBossAnchorFloorYd;
    }

    // Whether the seek pull may go for this Son: within the puller's radius plus the hit reach, and on the centre's
    // level. A Son below the platform or farther out is left for a later cycle; the puller never walks out for it.
    // No rounding slack: the pick reach plus the walk's clamp must leave the Son within the hit reach, or the puller
    // would stand on the circle walking to it forever.
    inline bool SeekPullSonInAnchor(const BossAnchorCircle& c, float x, float y, float z)
    {
        return BossAnchorDistance2d(c, x, y) <= c.radius + kBossAnchorSeekPullMarginYd &&
            std::fabs(z - c.z) <= kBossAnchorFloorYd;
    }

    // Where a ranged puller stands to shoot a Son: the Son's own point when it is inside the shooter's circle, else
    // the circle's point toward it. True when the Son is within `rangeYd` of that stand. The height rule is the tank
    // walk's only: a shot from the platform reaches a Son on the slope below, which then walks up to the raid.
    inline bool AnchorRangedShotStand(const BossAnchorCircle& c, float sonX, float sonY, float rangeYd, float& standX,
                                      float& standY)
    {
        standX = sonX;
        standY = sonY;
        ClampToAnchor(c, standX, standY);
        float const dx = sonX - standX;
        float const dy = sonY - standY;
        return std::sqrt(dx * dx + dy * dy) <= rangeYd;
    }

    // The anchored seek pull of one Son: a tank walks to it when it stands inside the tank's reach, else a ranged
    // puller shoots it from the anchor when the stand reaches it and sees it, else the cycle is skipped.
    enum class AnchorPull : std::uint8_t
    {
        TankWalk = 0,
        RangedFromAnchor = 1,
        Skip = 2,
    };

    inline AnchorPull AnchorSeekPull(const BossAnchorCircle& tank, float sonX, float sonY, float sonZ, bool shotInRange,
                                     bool standSeesSon)
    {
        if (SeekPullSonInAnchor(tank, sonX, sonY, sonZ))
            return AnchorPull::TankWalk;
        if (shotInRange && standSeesSon)
            return AnchorPull::RangedFromAnchor;
        return AnchorPull::Skip;
    }

    // Whether a bot may chase a hostile standing at (x, y): within the radius plus `reachYd` of the centre. A target
    // farther out is not chased (the bot holds; its ranged abilities or the target logic answer it).
    inline bool AnchorAllowsChase(const BossAnchorCircle& c, float x, float y, float reachYd)
    {
        return BossAnchorDistance2d(c, x, y) <= c.radius + reachYd;
    }

    // A hazard dodge under a live anchor: the dodge point (x, y) clamped onto the circle, kept only while the clamped
    // point is still clear of the hazard it dodges (centre hazardX/Y, range hazardYd). False: the move is refused this
    // tick, so the dodge never ends outside the circle.
    inline bool AnchorHazardDodge(const BossAnchorCircle& c, float hazardX, float hazardY, float hazardYd, float& x,
                                  float& y)
    {
        ClampToAnchor(c, x, y);
        float const dx = x - hazardX;
        float const dy = y - hazardY;
        return std::sqrt(dx * dx + dy * dy) > hazardYd;
    }

    // The chase's hazard dodge under a live anchor: the two candidates (left, then right) each go through
    // AnchorHazardDodge and then `accept` (the glue's floor, hazard and navigation checks on the clamped point); the
    // first that passes is (x, y) and its index is returned. -1: neither passes, the move is refused.
    template <typename Accept>
    inline int AnchorPickDodge(const BossAnchorCircle& c, float hazardX, float hazardY, float hazardYd, const float (&xs)[2],
                               const float (&ys)[2], float& x, float& y, Accept accept)
    {
        for (int i = 0; i < 2; ++i)
        {
            float px = xs[i];
            float py = ys[i];
            if (AnchorHazardDodge(c, hazardX, hazardY, hazardYd, px, py) && accept(px, py))
            {
                x = px;
                y = py;
                return i;
            }
        }
        return -1;
    }

    // Whether every point of a path (x, y pairs) stays inside the circle: the hazard-avoiding path inserts points
    // sideways of each hazard it meets, and one of them can lie outside.
    inline bool AnchorPathInside(const BossAnchorCircle& c, const std::vector<std::pair<float, float>>& path)
    {
        for (const std::pair<float, float>& point : path)
            if (!InsideAnchor(c, point.first, point.second))
                return false;
        return true;
    }

    // Whether a seeker may take a cloud: some part of it lies inside the circle (centre within radius + the cloud's
    // radius). A cloud wholly outside would hold the seeker at the circle's edge, chasing it.
    inline bool AnchorReachesCloud(const BossAnchorCircle& c, float cloudX, float cloudY, float cloudRadius)
    {
        return BossAnchorDistance2d(c, cloudX, cloudY) <= c.radius + cloudRadius;
    }
}
