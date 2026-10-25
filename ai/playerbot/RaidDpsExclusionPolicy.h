#pragma once
#include <cstdint>

namespace ai
{
    enum class RaidDpsEncounter { None, Golemagg, Majordomo, Garr, Razorgore };

    inline RaidDpsEncounter RaidDpsEncounterForTarget(std::uint32_t entry, bool golemagg,
        bool majordomo, bool garr = false, bool razorgorePhaseOne = false)
    {
        if (entry == 11672 && golemagg)
            return RaidDpsEncounter::Golemagg;
        if (entry == 12018 && majordomo)
            return RaidDpsEncounter::Majordomo;
        if (entry == 12099 && garr)
            return RaidDpsEncounter::Garr;
        if (entry == 12435 && razorgorePhaseOne)
            return RaidDpsEncounter::Razorgore;
        return RaidDpsEncounter::None;
    }

    inline bool RaidDpsTargetExcluded(bool enabled, bool inEncounterMap, bool tank,
        bool ordered, bool protectedEntry, bool encounterActive)
    {
        return enabled && inEncounterMap && !tank && !ordered && protectedEntry && encounterActive;
    }

    inline bool RazorgorePhaseOneTargetProtected(bool enabled, bool inBwl,
        bool isRazorgore, bool bossInCombat, bool orbFound, bool orbDisabled)
    {
        // If the orb cannot be observed, keep the opt-in protection active.
        // A missing object must not turn phase-one boss damage back on.
        return enabled && inBwl && isRazorgore && bossInCombat && (!orbFound || !orbDisabled);
    }
}
