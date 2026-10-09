
#include "playerbot/playerbot.h"
#include "DruidTriggers.h"
#include "DruidActions.h"

using namespace ai;

bool EntanglingRootsKiteTrigger::IsActive()
{
	if (!DebuffTrigger::IsActive())
		return false;

    if (AI_VALUE(uint8, "attackers count") > 3)
        return false;

	if (GetTarget()->GetPowerType() != POWER_MANA)
		return false;

    std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); i++)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit || !sServerFacade.IsAlive(unit))
            continue;

        if (ai->HasMyAura("entangling roots", unit))
            return false;
    }

    return !HasMaxDebuffs();
}

bool InFeralFormTrigger::IsActive()
{
    return ai->HasAura("bear form", bot) || ai->HasAura("dire bear form", bot) || ai->HasAura("cat form", bot);
}

bool FaerieFireFeralTrigger::IsActive()
{
    if (!sServerFacade.IsInCombat(bot))
        return false;

    // Bear: every cast is free threat/damage — spam it on any live target.
    if (ai->HasAura("bear form", bot) || ai->HasAura("dire bear form", bot))
    {
        Unit* target = GetTarget();
        return target && target->IsAlive() && target->IsInWorld();
    }

    if (!ai->HasAura("cat form", bot))
        return false;

    if (ai->HasAura("prowl", bot))
        return false;

    // Cat with Omen of Clarity: spam to fish for Clearcasting procs.
    if (ai->HasAura("omen of clarity", bot))
    {
        Unit* target = GetTarget();
        return target && target->IsAlive() && target->IsInWorld();
    }

    // Cat without Omen: apply as a normal debuff, don't reapply.
    return DebuffTrigger::IsActive();
}
