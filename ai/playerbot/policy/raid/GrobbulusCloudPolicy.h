#pragma once

#include <cmath>

// Pure decision rules for the Grobbulus fight (mod-playerbots parity:
// NaxxActions_Grobbulus.cpp, scoped to what ports without live coords).
// The universal bomb runout already covers injection carriers (28169 in
// the bomb list); this fight adds the ranged behind-boss leg and poison
// cloud (NPC 15933) step-out. Omitted deliberately: return-to-center
// (reach-to-attack re-engages automatically) and MT cloud rotation
// (no main-tank concept + ring coords need a live run). IDs verified in
// tw_world + core boss_grobbulus.cpp: Grobbulus 15931, cloud 15933.

namespace ai
{
// Ranged carrier goes behind the boss instead of just out: directly
// opposite the boss's facing, 18yd out (donor's go-behind runout is 24yd;
// 18yd is its melee move-away distance, kept here for DPS uptime).
// Poison cloud step-out radius (cloud ticks the clump).
inline constexpr float kGrobbulusCloudRadius = 10.0f;

// Behind-boss point: boss pos + 18yd along (boss facing + PI).
inline void GrobbulusBehindSpot(float bossX, float bossY, float bossFacing,
    float& outX, float& outY)
{
    outX = bossX - 18.0f * std::cos(bossFacing);
    outY = bossY - 18.0f * std::sin(bossFacing);
}
} // namespace ai

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace ai
{
inline float GrobbulusCloudRadius(int32_t maximumMs,int32_t remainingMs)
{
    // Without the aura, Turtle's spell retains its DBC30yd fallback radius.
    if (maximumMs<=0 || remainingMs<0) return 32.0f;
    float const age=std::max(0.0f,std::min(float(maximumMs),float(maximumMs)-remainingMs));
    float const predicted=std::min(float(maximumMs),age+3000.0f);
    return 2.0f+18.0f*predicted/maximumMs+2.0f;
}
struct GrobbulusCloudCircle { float x,y,radius; };
inline bool GrobbulusCloudContains(float x,float y,std::vector<GrobbulusCloudCircle> const& clouds)
{
    for (auto const& c:clouds)
        if (std::hypot(x-c.x,y-c.y)<=c.radius) return true;
    return false;
}
inline bool GrobbulusEscapeSample(float startX,float startY,float x,float y,
    std::vector<GrobbulusCloudCircle> const& clouds)
{
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    for (auto const& c:clouds)
    {
        float const start=std::hypot(startX-c.x,startY-c.y);
        float const sample=std::hypot(x-c.x,y-c.y);
        if (start>c.radius ? sample<=c.radius : sample+0.5f<start) return false;
    }
    return true;
}
}
