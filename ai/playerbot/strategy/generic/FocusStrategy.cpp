#include "playerbot/playerbot.h"
#include "FocusStrategy.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"

using namespace ai;

namespace
{
    // Review PR #584: the ACTION_THREAT_AOE mark covers almost no real area
    // spells in this tree (only blizzard carries it), so keying the veto on
    // the mark alone lets flamestrike, volley, hurricane etc. keep breaking
    // CC. Detect area damage from the spell data instead: any effect
    // enumerating enemy units in an area. Threat marks stay untouched (no
    // blast radius into the threat veto).
    bool IsEnemyAreaSpell(SpellEntry const* spellInfo)
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
        }

        return false;
    }
}

float FocusMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    // Heals are exempt from the whole burn (donor exempts
    // CastHealingSpellAction the same way).
    if (dynamic_cast<CastHealingSpellAction*>(action) != nullptr)
        return 1.0f;

    // Single-target burn: no area damage while crowd control is out.
    if (action->GetThreatType() == ActionThreatType::ACTION_THREAT_AOE)
        return 0.0f;

    // The threat mark misses most real area spells, so read the spell data
    // too. Non-spell actions (melee abilities, traps) have no spell id and
    // are left alone here — same coverage limit as the donor.
    if (dynamic_cast<CastSpellAction*>(action) != nullptr)
    {
        uint32 spellId = AI_VALUE2(uint32, "spell id", action->GetName());
        if (IsEnemyAreaSpell(sServerFacade.LookupSpellInfo(spellId)))
            return 0.0f;
    }

    // No debuffs on arbitrary attackers either: they are picked off-target
    // and break CC the same way AoE does. Donor vetoes only the ranged
    // sibling (CastDebuffSpellOnAttackerAction); the melee sibling
    // (CastDebuffSpellOnMeleeAttackerAction, e.g. rend/disarm on an
    // independent melee add) stays allowed — exact parity.
    if (dynamic_cast<CastRangedDebuffSpellOnAttackerAction*>(action) != nullptr)
        return 0.0f;

    return 1.0f;
}

void FocusStrategy::InitCombatMultipliers(std::list<Multiplier*> &multipliers)
{
    multipliers.push_back(new FocusMultiplier(ai));
}
