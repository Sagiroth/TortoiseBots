#include "../ai/playerbot/RaidDpsExclusionPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace ai;
    assert(RaidDpsTargetExcluded(true, true, false, false, true, true));
    assert(!RaidDpsTargetExcluded(false, true, false, false, true, true));
    assert(!RaidDpsTargetExcluded(true, false, false, false, true, true));
    assert(!RaidDpsTargetExcluded(true, true, true, false, true, true));
    assert(!RaidDpsTargetExcluded(true, true, false, true, true, true));
    assert(!RaidDpsTargetExcluded(true, true, false, false, false, true));
    assert(!RaidDpsTargetExcluded(true, true, false, false, true, false));
    // Roles and orders change immediately; no remembered blacklist survives.
    for (int pull = 0; pull < 10; ++pull)
    {
        assert(!RaidDpsTargetExcluded(true, true, false, false, true, false));
        assert(RaidDpsTargetExcluded(true, true, false, false, true, true));
        assert(!RaidDpsTargetExcluded(true, true, true, false, true, true));
        assert(!RaidDpsTargetExcluded(true, true, false, true, true, true));
        assert(!RaidDpsTargetExcluded(true, true, false, false, true, false));
    }
    std::cout << "Raid DPS exclusion policy: passed\n";
}
