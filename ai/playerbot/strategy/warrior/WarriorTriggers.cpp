
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

    // Stop at a full 5-stack: re-sunder only to refresh, not to stack.
    // (Refresh timing stays with the aura-expiry check in the action; the
    // old tank-always-true burned a GCD + 15 rage on every tick forever.)
    if (Aura* aura = ai->GetAura("sunder armor", target))
    {
        if (aura->GetStackAmount() >= 5)
            return false;
        return true;
    }

    if (ai->IsTank(bot) && !target->IsPlayer())
        return true;

    return !ai->HasAura("sunder armor", target, true) && !HasMaxDebuffs();
}
