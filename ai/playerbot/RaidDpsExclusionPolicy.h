#pragma once

namespace ai
{
    inline bool RaidDpsTargetExcluded(bool enabled, bool inEncounterMap, bool tank,
        bool ordered, bool protectedEntry, bool encounterActive)
    {
        return enabled && inEncounterMap && !tank && !ordered && protectedEntry && encounterActive;
    }
}
