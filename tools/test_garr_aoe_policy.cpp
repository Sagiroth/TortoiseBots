#include "../ai/playerbot/GarrAoePolicy.h"
#include "../ai/playerbot/RaidDpsExclusionPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace ai;
    for (char const* action : {"multi-shot", "chain lightning", "cleave", "sweeping strikes",
        "blade flurry", "hurricane", "holy nova", "consecration", "searing totem",
        "magma totem", "explosive trap", "explosive trap on target", "screech"})
        assert(GarrIncidentalDamageAction(action, false, false, false));
    assert(GarrIncidentalDamageAction("blizzard", true, false, false));
    assert(GarrIncidentalDamageAction("corruption on attacker", true, false, false));
    assert(!GarrIncidentalDamageAction("chain heal", true, true, false));
    assert(!GarrIncidentalDamageAction("prayer of healing", true, true, false));
    assert(!GarrIncidentalDamageAction("banish", true, false, true));
    assert(!GarrIncidentalDamageAction("heal party", false, true, false));
    assert(!GarrIncidentalDamageAction("frostbolt", false, false, false));
    assert(!GarrIncidentalDamageAction("tranquilizing shot", false, false, false));
    assert(!GarrIncidentalDamageAction("freezing trap", false, false, true));
    assert(RaidDpsEncounterForTarget(12099, false, false, true) == RaidDpsEncounter::Garr);
    assert(RaidDpsEncounterForTarget(12099, true, true, false) == RaidDpsEncounter::None);
    assert(RaidDpsEncounterForTarget(12057, true, true, true) == RaidDpsEncounter::None);
    assert(!RaidDpsTargetExcluded(true, true, true, false, true, true)); // tanks
    assert(!RaidDpsTargetExcluded(true, true, false, true, true, true)); // assigned add
    std::cout << "Garr AoE policy: passed\n";
}
