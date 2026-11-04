#pragma once

#include "SharedDefines.h"

// The aura types the core counts as a DoT's periodic damage: the damage tick, the
// leech tick and the percent-of-max damage tick. A guard that reads only
// SPELL_AURA_PERIODIC_DAMAGE misses the other two.

namespace ai
{
    constexpr uint32 kPeriodicDamageAuraTypes[] =
    {
        SPELL_AURA_PERIODIC_DAMAGE,
        SPELL_AURA_PERIODIC_LEECH,
        SPELL_AURA_PERIODIC_DAMAGE_PERCENT,
    };

    inline bool IsPeriodicDamageAura(uint32 auraType)
    {
        for (uint32 const type : kPeriodicDamageAuraTypes)
            if (type == auraType)
                return true;
        return false;
    }
}
