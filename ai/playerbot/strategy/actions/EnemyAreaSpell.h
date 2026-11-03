#pragma once
#include "playerbot/playerbot.h"

namespace ai
{
    // Whether `spellInfo` damages enemies in an area. The ACTION_THREAT_AOE mark covers almost no real area spell in
    // this tree (only blizzard carries it), so keying a veto on the mark alone lets flamestrike, volley, hurricane and
    // the like through: detect area damage from the spell data instead, any effect enumerating enemy units in an area.
    // Threat marks stay untouched (no blast radius into the threat veto).
    //
    // Review PR #584: chains resolve via single-enemy target + jump count (core Spell.cpp:2085-2086,2369-2393), not
    // via area enums, so a chain lightning jump breaks a sheep without ever looking like an area spell. Chain heal is
    // safe: healing actions return early in the callers.
    inline bool IsEnemyAreaSpell(SpellEntry const* spellInfo)
    {
        if (!spellInfo)
            return false;

        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            uint32 target = spellInfo->EffectImplicitTargetA[i];
            if (target == TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC ||
                target == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC ||
                target == TARGET_ENUM_UNITS_ENEMY_IN_CONE_24 ||
                target == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DYNOBJ_LOC ||
                target == TARGET_ENUM_UNITS_ENEMY_WITHIN_CASTER_RANGE)
                return true;
            if (spellInfo->EffectChainTarget[i] > 1)
                return true;
        }

        return false;
    }
}
