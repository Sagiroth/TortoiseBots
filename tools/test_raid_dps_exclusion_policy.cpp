#include "../ai/playerbot/RaidDpsExclusionPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace ai;
    assert(RaidDpsEncounterForTarget(11672, true, false) == RaidDpsEncounter::Golemagg);
    assert(RaidDpsEncounterForTarget(11672, false, true) == RaidDpsEncounter::None);
    assert(RaidDpsEncounterForTarget(12018, false, true) == RaidDpsEncounter::Majordomo);
    assert(RaidDpsEncounterForTarget(12018, true, false) == RaidDpsEncounter::None);
    for (auto entry : {11663u, 11664u, 11988u, 12056u})
        assert(RaidDpsEncounterForTarget(entry, true, true) == RaidDpsEncounter::None);
    assert(RaidDpsEncounterForTarget(12435, false, false, false, true) == RaidDpsEncounter::Razorgore);
    assert(RaidDpsEncounterForTarget(12435, false, false) == RaidDpsEncounter::None);
    assert(RaidDpsEncounterForTarget(12018, false, false) == RaidDpsEncounter::None);
    assert(RaidDpsTargetExcluded(true, true, false, false, true, true));
    assert(!RaidDpsTargetExcluded(false, true, false, false, true, true));
    assert(!RaidDpsTargetExcluded(true, false, false, false, true, true));
    assert(!RaidDpsTargetExcluded(true, true, true, false, true, true));
    assert(!RaidDpsTargetExcluded(true, true, false, true, true, true));
    assert(!RaidDpsTargetExcluded(true, true, false, false, false, true));
    assert(!RaidDpsTargetExcluded(true, true, false, false, true, false));

    assert(RazorgorePhaseOneTargetProtected(true, true, true, true, true, false));
    assert(!RazorgorePhaseOneTargetProtected(false, true, true, true, true, false));
    assert(!RazorgorePhaseOneTargetProtected(true, false, true, true, true, false));
    assert(!RazorgorePhaseOneTargetProtected(true, true, false, true, true, false));
    assert(!RazorgorePhaseOneTargetProtected(true, true, true, false, true, false));
    assert(RazorgorePhaseOneTargetProtected(true, true, true, true, false, false));
    assert(!RazorgorePhaseOneTargetProtected(true, true, true, true, true, true));
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
