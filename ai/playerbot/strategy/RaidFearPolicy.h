#pragma once

#include <cstdint>

namespace ai
{
    constexpr uint32_t RAID_MAGMADAR_MAP_ID = 409;
    constexpr uint32_t RAID_ONYXIA_MAP_ID = 249;
    constexpr uint32_t RAID_BLACKWING_LAIR_MAP_ID = 469;
    constexpr uint32_t MAGMADAR_CREATURE_ENTRY = 11982;
    constexpr uint32_t ONYXIA_CREATURE_ENTRY = 10184;
    constexpr uint32_t NEFARIAN_CREATURE_ENTRY = 11583;

    inline bool IsRaidFearEncounter(uint32_t mapId, uint32_t creatureEntry)
    {
        return (mapId == RAID_MAGMADAR_MAP_ID && creatureEntry == MAGMADAR_CREATURE_ENTRY) ||
            (mapId == RAID_ONYXIA_MAP_ID && creatureEntry == ONYXIA_CREATURE_ENTRY) ||
            (mapId == RAID_BLACKWING_LAIR_MAP_ID && creatureEntry == NEFARIAN_CREATURE_ENTRY);
    }
}
