#pragma once

#include "Objects/Unit.h"

namespace ai
{
    // mob_core_hound::FeignDeath keeps the engine death state ALIVE, threat
    // and victim while DamageTaken absorbs damage. ResurrectSelf restores HP.
    inline bool IsCollapsedCoreHound(Unit const* target)
    {
        return target && target->IsCreature() && target->GetMapId() == 409 &&
            target->GetEntry() == 11671 && target->GetHealth() == 0 &&
            target->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);
    }
}
