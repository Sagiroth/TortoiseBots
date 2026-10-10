
#include "playerbot/playerbot.h"
#include "playerbot/GroupMembers.h"
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

bool HealerLowManaTrigger::IsActive()
{
    // Innervate unknown or on cooldown: stay quiet so the row never queues
    // a cast that fails after the caster-form shift (mirrors
    // SpellTargetTrigger::IsSpellReady).
    uint32 innervateId = AI_VALUE2(uint32, "spell id", "innervate");
    if (!innervateId || !sServerFacade.IsSpellReady(bot, innervateId))
        return false;
    // Mirror of the healer scan in CastInnervateAction::GetTarget: any
    // living same-map party healer (not self) below the LowMana line.
    // Uses ai->IsHeal so role detection matches the action exactly.
    // Skips healers already carrying Innervate (the action's auraCheck
    // would refuse them) and anyone out of spell range (the action has no
    // reach prerequisite, so an unreachable healer only burns the shift).
    float maxRange = 0.0f;
    if (!ai->GetSpellRange("innervate", &maxRange))
        return false;
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == bot || !ai->IsSafe(member) || !ai->IsHeal(member))
            continue;
        if (member->GetMapId() != bot->GetMapId() || !sServerFacade.IsAlive(member))
            continue;
        if (!member->GetMaxPower(POWER_MANA))
            continue;
        if (ai->HasAura("innervate", member))
            continue;
        if (sServerFacade.getDistance2d(bot, member) > maxRange)
            continue;
        if (ai->GetManaPercent(*member) < sPlayerbotAIConfig.lowMana)
            return true;
    }
    return false;
}
