#pragma once
#include <cmath>
namespace ai
{
// Turtle28785 triggers28786; all three damage/debuff effects use radius row10 (30yd).
constexpr float kLocustSwarmClearance = 33.0f;
inline bool LocustSwarmEligible(bool enabled, bool ready, bool inNaxx, bool combat, bool tank, bool hold)
{
    return enabled && ready && inNaxx && combat && !tank && !hold;
}
inline bool LocustSwarmNeedsRetreat(float distance)
{
    return std::isfinite(distance) && distance < kLocustSwarmClearance;
}
inline bool LocustSwarmBlocksAction(bool movement, bool charge, bool escape)
{
    return (movement || charge) && !escape;
}
}
