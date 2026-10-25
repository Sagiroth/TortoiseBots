#include "../ai/playerbot/strategy/RaidCurePolicy.h"

#include <iostream>

int main()
{
    using ai::IsRaidCureInterlocked;
    using ai::MUTATING_INJECTION_SPELL_ID;
    using ai::NAXXRAMAS_MAP_ID;

    if (!IsRaidCureInterlocked(NAXXRAMAS_MAP_ID, true))
        return 1;
    if (IsRaidCureInterlocked(NAXXRAMAS_MAP_ID, false))
        return 1;
    if (IsRaidCureInterlocked(469, true))
        return 1;
    if (MUTATING_INJECTION_SPELL_ID != 28169 || NAXXRAMAS_MAP_ID != 533)
        return 1;

    std::cout << "raid cure policy: OK\n";
    return 0;
}
