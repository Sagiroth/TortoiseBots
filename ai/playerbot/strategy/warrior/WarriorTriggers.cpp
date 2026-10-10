
#include "playerbot/playerbot.h"
#include "WarriorTriggers.h"
#include "WarriorActions.h"

using namespace ai;

bool BloodrageBuffTrigger::IsActive()
{
    if (!ai->HasSpell("bloodrage"))
        return false;

    return AI_VALUE2(uint8, "health", "self target") >= sPlayerbotAIConfig.mediumHealth &&
        AI_VALUE2(uint8, "rage", "self target") < 20;
}

bool SunderArmorDebuffTrigger::IsActive()
{
    if (!ai->HasSpell("sunder armor"))
        return false;

    Unit* target = GetTarget();
    if (!target)
        return false;

    // Stop at a full 5-stack, but re-arm to refresh an expiring stack
    // (donor refreshes at <=6s remaining): without this the 5-stack falls
    // off completely before the trigger fires again.
    if (Aura* aura = ai->GetAura("sunder armor", target))
    {
        if (aura->GetStackAmount() >= 5)
            return aura->GetAuraDuration() <= 6000;
        return true;
    }

    if (ai->IsTank(bot) && !target->IsPlayer())
        return true;

    return !ai->HasAura("sunder armor", target, true) && !HasMaxDebuffs();
}
