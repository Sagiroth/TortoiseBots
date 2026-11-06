#pragma once

// The boss anchor (`ai_boss_tactic` kind `anchor`, docs/play/boss-tactics.md): while its boss fights, every movement
// destination the boss and raid code picks stays inside a circle around the boss's home. Hakkar's script evades him
// the moment he steps below z 45.8 (boss_hakkar.cpp), and that line runs 45-50 yd SW of his spawn; a raid standing on
// the slope drags him over it on every target swap.
//
// Pure: the glue (ClampMoveToBossAnchor in strategy/actions/BossTacticsActions.cpp) reads the row, the bot's role and
// the floor, and every mover calls it. Tested in tools/test_boss_anchor_policy.cpp.

#include <cmath>

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
    inline bool SeekPullSonInAnchor(const BossAnchorCircle& c, float x, float y, float z)
    {
        return BossAnchorDistance2d(c, x, y) <= c.radius + kBossAnchorSeekPullMarginYd + kBossAnchorEpsYd &&
            std::fabs(z - c.z) <= kBossAnchorFloorYd;
    }
}
