#include "../ai/playerbot/strategy/RaidDispelPolicy.h"

#include <iostream>

int main()
{
    using ai::IsPriorityRaidDispel;
    using ai::LUCIFRONS_CURSE_SPELL_ID;
    using ai::MAGMADAR_PANIC_SPELL_ID;
    using ai::NEFARIAN_WILD_POLYMORPH_SPELL_ID;
    using ai::RAID_DISPEL_CURSE;
    using ai::RAID_DISPEL_MAGIC;
    using ai::RAID_BLACKWING_LAIR_MAP_ID;
    using ai::RAID_MOLTEN_CORE_MAP_ID;

    if (!IsPriorityRaidDispel(RAID_MOLTEN_CORE_MAP_ID, LUCIFRONS_CURSE_SPELL_ID, RAID_DISPEL_CURSE) ||
        !IsPriorityRaidDispel(RAID_MOLTEN_CORE_MAP_ID, MAGMADAR_PANIC_SPELL_ID, RAID_DISPEL_MAGIC) ||
        !IsPriorityRaidDispel(RAID_BLACKWING_LAIR_MAP_ID, NEFARIAN_WILD_POLYMORPH_SPELL_ID, RAID_DISPEL_MAGIC))
        return 1;
    if (IsPriorityRaidDispel(RAID_MOLTEN_CORE_MAP_ID, LUCIFRONS_CURSE_SPELL_ID, RAID_DISPEL_MAGIC) ||
        IsPriorityRaidDispel(RAID_BLACKWING_LAIR_MAP_ID, MAGMADAR_PANIC_SPELL_ID, RAID_DISPEL_MAGIC) ||
        IsPriorityRaidDispel(RAID_MOLTEN_CORE_MAP_ID, NEFARIAN_WILD_POLYMORPH_SPELL_ID, RAID_DISPEL_MAGIC) ||
        IsPriorityRaidDispel(533, LUCIFRONS_CURSE_SPELL_ID, RAID_DISPEL_CURSE))
        return 1;

    if (!IsPriorityRaidDispel(533, 29213, RAID_DISPEL_CURSE))
    {
        std::cerr << "Noth curse must enter the priority pass in Naxxramas\n";
        return 1;
    }
    if (IsPriorityRaidDispel(409, 29213, RAID_DISPEL_CURSE) ||
        IsPriorityRaidDispel(533, 29213, RAID_DISPEL_MAGIC) ||
        IsPriorityRaidDispel(533, 29214, RAID_DISPEL_CURSE))
        return 1;

    std::cout << "raid dispel priority policy: OK\n";
    return 0;
}
