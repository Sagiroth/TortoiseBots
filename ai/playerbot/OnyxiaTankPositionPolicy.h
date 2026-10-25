#pragma once
#include <cmath>
#include <algorithm>
#include <vector>

namespace ai
{
struct OnyxiaTankPoint { float x, y, z; };
// Northern ground point in core boss_onyxia.cpp::aMoveData (not flight Z).
constexpr OnyxiaTankPoint kOnyxiaTankSpot{22.8763f, -217.152f, -85.054054f};

inline float OnyxiaTankDistance(OnyxiaTankPoint a, OnyxiaTankPoint b)
{
    return std::sqrt((a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y) + (a.z-b.z)*(a.z-b.z));
}

inline bool OnyxiaTankCorridor(OnyxiaTankPoint p)
{
    // Conservative central strip. Core whelp mouths are at y=-254.463 and
    // -177.106; this keeps every point over 20 yards from both mouths.
    // Convex bounds also protect the segments between validated vertices.
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        p.x >= -110.0f && p.x <= 35.0f && p.y >= -232.0f && p.y <= -198.0f &&
        p.z >= -94.0f && p.z <= -80.0f;
}

inline bool OnyxiaTankRouteSafe(OnyxiaTankPoint start, std::vector<OnyxiaTankPoint> const& path, OnyxiaTankPoint destination = kOnyxiaTankSpot)
{
    if (!OnyxiaTankCorridor(start) || path.size() < 2 ||
        OnyxiaTankDistance(start, path.front()) > 2.0f ||
        OnyxiaTankDistance(destination, path.back()) > 1.5f)
        return false;
    float length = 0.0f;
    auto previous = start;
    for (auto const& point : path)
    {
        if (!OnyxiaTankCorridor(point)) return false;
        length += OnyxiaTankDistance(previous, point);
        previous = point;
    }
    return length <= 165.0f;
}
inline OnyxiaTankPoint OnyxiaOpeningStep(OnyxiaTankPoint here, OnyxiaTankPoint next, float speed, unsigned elapsed)
{
    float distance=OnyxiaTankDistance(here,next);
    float budget=std::max(0.f,speed)*std::min(elapsed,200u)/1000.f;
    float fraction=distance>.01f ? std::min(1.f,budget/distance):1.f;
    return {here.x+(next.x-here.x)*fraction,here.y+(next.y-here.y)*fraction,here.z+(next.z-here.z)*fraction};
}
}
