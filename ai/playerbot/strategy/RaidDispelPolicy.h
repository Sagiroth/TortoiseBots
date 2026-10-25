#pragma once

#include <cstdint>

namespace ai
{
    constexpr uint32_t RAID_MOLTEN_CORE_MAP_ID = 409;
    constexpr uint32_t RAID_BLACKWING_LAIR_MAP_ID = 469;
    constexpr uint32_t LUCIFRONS_CURSE_SPELL_ID = 19703;
    constexpr uint32_t MAGMADAR_PANIC_SPELL_ID = 19408;
    constexpr uint32_t NEFARIAN_WILD_POLYMORPH_SPELL_ID = 23603;
    constexpr uint32_t RAID_DISPEL_MAGIC = 1;
    constexpr uint32_t RAID_DISPEL_CURSE = 2;

    inline bool IsPriorityRaidDispel(uint32_t mapId, uint32_t auraSpellId, uint32_t dispelType)
    {
        return (mapId == RAID_MOLTEN_CORE_MAP_ID && auraSpellId == LUCIFRONS_CURSE_SPELL_ID && dispelType == RAID_DISPEL_CURSE) ||
            (mapId == RAID_MOLTEN_CORE_MAP_ID && auraSpellId == MAGMADAR_PANIC_SPELL_ID && dispelType == RAID_DISPEL_MAGIC) ||
            (mapId == RAID_BLACKWING_LAIR_MAP_ID && auraSpellId == NEFARIAN_WILD_POLYMORPH_SPELL_ID && dispelType == RAID_DISPEL_MAGIC);
    }
}
