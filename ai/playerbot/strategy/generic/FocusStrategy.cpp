#include "playerbot/playerbot.h"
#include "FocusStrategy.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"

using namespace ai;

float FocusMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    // Single-target burn: no area damage while crowd control is out. Heals
    // are exempt (donor exempts CastHealingSpellAction the same way).
    if (action->GetThreatType() == ActionThreatType::ACTION_THREAT_AOE &&
        dynamic_cast<CastHealingSpellAction*>(action) == nullptr)
        return 0.0f;

    // No debuffs on arbitrary attackers either: they are picked off-target
    // and break CC the same way AoE does (donor vetoes
    // CastDebuffSpellOnAttackerAction; ours splits it in melee/ranged).
    if (dynamic_cast<CastMeleeDebuffSpellOnAttackerAction*>(action) != nullptr ||
        dynamic_cast<CastRangedDebuffSpellOnAttackerAction*>(action) != nullptr)
        return 0.0f;

    return 1.0f;
}

void FocusStrategy::InitCombatMultipliers(std::list<Multiplier*> &multipliers)
{
    multipliers.push_back(new FocusMultiplier(ai));
}
