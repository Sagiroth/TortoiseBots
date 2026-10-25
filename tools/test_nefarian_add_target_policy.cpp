#include "../ai/playerbot/strategy/NefarianAddTargetPolicy.h"

#include <iostream>

int main()
{
    using ai::IsNefarianClassCallAdd;
    using ai::NEFARIAN_CORRUPTED_INFERNAL_ENTRY;
    using ai::NEFARIAN_ADD_MAP_ID;
    using ai::NEFARIAN_BOSS_ENTRY;

    if (!IsNefarianClassCallAdd(NEFARIAN_ADD_MAP_ID, NEFARIAN_BOSS_ENTRY,
            NEFARIAN_CORRUPTED_INFERNAL_ENTRY) ||
        IsNefarianClassCallAdd(0, NEFARIAN_BOSS_ENTRY, NEFARIAN_CORRUPTED_INFERNAL_ENTRY) ||
        IsNefarianClassCallAdd(NEFARIAN_ADD_MAP_ID, 10184, NEFARIAN_CORRUPTED_INFERNAL_ENTRY) ||
        IsNefarianClassCallAdd(NEFARIAN_ADD_MAP_ID, NEFARIAN_BOSS_ENTRY, 1))
        return 1;

    std::cout << "Nefarian class-call add policy: OK\n";
}
