#pragma once
#include <cstdint>

namespace ai
{
    enum class RaidDpsEncounter { None, Golemagg, Majordomo };

    inline RaidDpsEncounter RaidDpsEncounterForTarget(std::uint32_t entry, bool golemagg, bool majordomo)
    {
        if (entry == 11672 && golemagg)
            return RaidDpsEncounter::Golemagg;
        if (entry == 12018 && majordomo)
            return RaidDpsEncounter::Majordomo;
        return RaidDpsEncounter::None;
    }

    inline bool RaidDpsTargetExcluded(bool enabled, bool inEncounterMap, bool tank,
        bool ordered, bool protectedEntry, bool encounterActive)
    {
        return enabled && inEncounterMap && !tank && !ordered && protectedEntry && encounterActive;
    }
}
