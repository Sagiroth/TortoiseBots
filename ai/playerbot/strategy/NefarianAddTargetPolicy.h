#pragma once

#include <cstdint>

namespace ai
{
    constexpr uint32_t NEFARIAN_ADD_MAP_ID = 469;
    constexpr uint32_t NEFARIAN_BOSS_ENTRY = 11583;
    constexpr uint32_t NEFARIAN_CORRUPTED_INFERNAL_ENTRY = 14668;

    inline bool IsNefarianClassCallAdd(uint32_t mapId, uint32_t bossEntry,
        uint32_t candidateEntry)
    {
        return mapId == NEFARIAN_ADD_MAP_ID && bossEntry == NEFARIAN_BOSS_ENTRY &&
            candidateEntry == NEFARIAN_CORRUPTED_INFERNAL_ENTRY;
    }
}
