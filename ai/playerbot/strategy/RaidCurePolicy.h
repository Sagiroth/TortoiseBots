#pragma once

#include <cstdint>

namespace ai
{
    constexpr uint32_t NAXXRAMAS_MAP_ID = 533;
    constexpr uint32_t MUTATING_INJECTION_SPELL_ID = 28169;

    // True means an automated cure should wait. Movement can independently
    // use or extend this decision without coupling the two policies.
    inline bool IsRaidCureInterlocked(uint32_t mapId, bool hasMutatingInjection)
    {
        return mapId == NAXXRAMAS_MAP_ID && hasMutatingInjection;
    }
}
