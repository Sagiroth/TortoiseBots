#include "../ai/playerbot/strategy/RaidFearPolicy.h"

#include <iostream>

int main()
{
    using ai::IsRaidFearEncounter;
    using ai::MAGMADAR_CREATURE_ENTRY;
    using ai::ONYXIA_CREATURE_ENTRY;
    using ai::RAID_MAGMADAR_MAP_ID;
    using ai::RAID_ONYXIA_MAP_ID;

    if (!IsRaidFearEncounter(RAID_MAGMADAR_MAP_ID, MAGMADAR_CREATURE_ENTRY) ||
        !IsRaidFearEncounter(RAID_ONYXIA_MAP_ID, ONYXIA_CREATURE_ENTRY) ||
        IsRaidFearEncounter(RAID_MAGMADAR_MAP_ID, ONYXIA_CREATURE_ENTRY) ||
        IsRaidFearEncounter(RAID_ONYXIA_MAP_ID, MAGMADAR_CREATURE_ENTRY) ||
        IsRaidFearEncounter(409, 1))
        return 1;

    std::cout << "raid fear encounter policy: OK\n";
}
