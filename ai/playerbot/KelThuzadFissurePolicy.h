#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ai
{
    constexpr uint32_t KT_FISSURE_ENTRY = 16129;
    // Void Blast 27812 uses SpellRadius row 8 (5 yards). Add two yards.
    constexpr float KT_FISSURE_CLEARANCE = 7.0f;
    struct KtFissureSnapshot
    {
        bool initialized = false;
        uint32_t map = 0, instance = 0, refreshedAt = 0;
        std::vector<uint64_t> guids;
        bool NeedsRefresh(uint32_t now, uint32_t mapId, uint32_t instanceId) const
        {
            return !initialized || map != mapId || instance != instanceId ||
                uint32_t(now - refreshedAt) >= 500;
        }
    };
    struct KtFissurePoint { float x, y; };
    struct KtFissureCircle { float x, y, radius; };
    inline bool KtFissureSegmentSafe(KtFissurePoint a, KtFissurePoint b, KtFissureCircle c)
    {
        if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) ||
            !std::isfinite(b.y) || !std::isfinite(c.x) || !std::isfinite(c.y) ||
            !std::isfinite(c.radius) || c.radius <= 0) return false;
        float const x = a.x - c.x, y = a.y - c.y;
        float const dx = b.x - a.x, dy = b.y - a.y;
        float const dot = x * dx + y * dy, length2 = dx * dx + dy * dy;
        float const radius2 = c.radius * c.radius;
        // Starting inside: every segment must head outward, never deeper.
        if (x * x + y * y < radius2) return dot >= 0.0f;
        float const t = length2 > 0 ? std::max(0.0f, std::min(1.0f, -dot / length2)) : 0;
        return (x + t * dx) * (x + t * dx) + (y + t * dy) * (y + t * dy) >= radius2;
    }
}
