#pragma once

#include <cstdint>

namespace ai
{
    constexpr uint32_t AQ40_MAP_ID = 531;
    constexpr uint32_t VISCIDUS_ENTRY = 15299;
    constexpr uint32_t VISCIDUS_GLOB_ENTRY = 15667;

    inline bool ShouldFocusViscidusGlob(uint32_t mapId, bool optedIn, bool botAlive,
        bool botInCombat, bool botIsTank, uint32_t candidateEntry)
    {
        return mapId == AQ40_MAP_ID && optedIn && botAlive && botInCombat && !botIsTank &&
            candidateEntry == VISCIDUS_GLOB_ENTRY;
    }

    // Validate lazily: ordinary targets must never request the nearby scan or
    // group CC checks merely because current-target validation runs.
    template<class Validate>
    inline bool CanUseViscidusGlobTarget(uint32_t mapId, bool optedIn, bool botAlive,
        bool botInCombat, bool botIsTank, uint32_t candidateEntry, Validate validate)
    {
        return ShouldFocusViscidusGlob(mapId, optedIn, botAlive, botInCombat,
            botIsTank, candidateEntry) && validate();
    }
}
