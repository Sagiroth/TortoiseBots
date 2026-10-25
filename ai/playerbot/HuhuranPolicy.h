#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace ai
{
// Center distances: keep room for the designated closest fifteen, but do not
// chase a scattered/dead soaker roster beyond ordinary healing range.
inline float HuhuranRangedRadius(std::vector<float> soakers)
{
    soakers.erase(std::remove_if(soakers.begin(), soakers.end(),
        [](float d) { return !std::isfinite(d) || d < 0; }), soakers.end());
    if (soakers.size() < 15) return 24.0f;
    std::nth_element(soakers.begin(), soakers.begin() + 14, soakers.end());
    return std::max(24.0f, std::min(28.0f, soakers[14] + 3.0f));
}
inline bool HuhuranNeedsSpace(float distance, float radius)
{
    return std::isfinite(distance) && distance < radius - 1.0f;
}
}
