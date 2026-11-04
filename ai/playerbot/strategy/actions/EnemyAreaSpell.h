#pragma once
#include "playerbot/playerbot.h"

#include <algorithm>

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

    // The enemy area of `spellInfo` as the crowd-control guards need it: whether
    // it stands at the cast's destination (else around the caster) and its radius
    // in yards. False when the spell hits no enemy area. Same shape rules as
    // BreaksCcInArea (GenericSpellActions.cpp): dest/ground areas use the spell's
    // target, caster-centred areas and cones the caster, chains jump from the
    // target.
    inline bool EnemyAreaOf(SpellEntry const* spellInfo, bool& atTarget, float& radius)
    {
        if (!spellInfo)
            return false;
        bool area = false;
        atTarget = false;
        radius = 0.0f;
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            uint32 const targetA = spellInfo->EffectImplicitTargetA[i];
            uint32 const targetB = spellInfo->EffectImplicitTargetB[i];
            uint32 const enemyArea =
                (targetA == TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC || targetA == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC ||
                 targetA == TARGET_ENUM_UNITS_ENEMY_IN_CONE_24 || targetA == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DYNOBJ_LOC ||
                 targetA == TARGET_ENUM_UNITS_ENEMY_WITHIN_CASTER_RANGE) ? targetA :
                (targetB == TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC || targetB == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC ||
                 targetB == TARGET_ENUM_UNITS_ENEMY_IN_CONE_24 || targetB == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DYNOBJ_LOC ||
                 targetB == TARGET_ENUM_UNITS_ENEMY_WITHIN_CASTER_RANGE) ? targetB : 0;
            float effectRadius = 0.0f;
            if (SpellRadiusEntry const* radiusEntry = sSpellRadiusStore.LookupEntry(spellInfo->EffectRadiusIndex[i]))
                effectRadius = radiusEntry->Radius;
            if (enemyArea)
            {
                area = true;
                // The pair goes together: the widest effect decides both the radius and where it is centred, so a
                // spell with a destination area and a caster-centred one is judged at the right place.
                bool const atDest = (enemyArea == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC ||
                    enemyArea == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DYNOBJ_LOC) && targetA != TARGET_LOCATION_CASTER_SRC &&
                    targetA != TARGET_LOCATION_CASTER_DEST;
                if (effectRadius <= 0.0f)
                    if (SpellRangeEntry const* rangeEntry = sSpellRangeStore.LookupEntry(spellInfo->rangeIndex))
                        effectRadius = rangeEntry->maxRange;
                if (effectRadius > radius)
                {
                    radius = effectRadius;
                    atTarget = atDest;
                }
            }
            else if (targetA == TARGET_UNIT_ENEMY && spellInfo->EffectChainTarget[i] > 1)
            {
                area = true;
                float const chainRadius = spellInfo->DmgClass == SPELL_DAMAGE_CLASS_MELEE ?
                    float(ATTACK_DISTANCE) : float(CHAIN_SPELL_JUMP_RADIUS);
                if (chainRadius > radius)
                {
                    radius = chainRadius;
                    atTarget = true; // a chain jumps from the target
                }
            }
        }
        return area;
    }
}
