#pragma once

// The threat reference a damage dealer's threat percentage is measured against (ThreatValue::GetTankThreat): the
// highest tank's threat, except on a target the core makes immune to taunts, where no tank can take it back and the
// raid's second-highest tank has to stay second so the boss turns to it on a threat reset. Tested in
// tools/test_threat_cap_policy.cpp; the caller passes the living tanks' threat as plain numbers, so this header stays
// free of core includes.

#include <algorithm>
#include <cstddef>
#include <functional>
#include <vector>

namespace ai
{
    // -1 when no tank is on the target (the caller's own "no reference" answer, kept from the previous max scan).
    inline float ThreatCapReference(std::vector<float> tankThreats, bool tauntImmune)
    {
        if (tankThreats.empty())
            return -1.0f;
        std::sort(tankThreats.begin(), tankThreats.end(), std::greater<float>());
        const std::size_t index = tauntImmune && tankThreats.size() >= 2 ? 1 : 0;
        return tankThreats[index];
    }
}
