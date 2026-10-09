
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

bool OocRebirthTrigger::IsActive()
{
    // A living priest, paladin or shaman in the group resurrects with a
    // normal out-of-combat spell; their rez is always preferred over our
    // 30 min battle rez. Scan first (cheap early-outs before Rebirth's
    // own cooldown/spellbook checks in the base IsActive below).
    Group* group = bot->GetGroup();
    if (group)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || member == bot || !sServerFacade.IsAlive(member))
                continue;
            uint8 cls = member->GetClass();
            if (cls == CLASS_PRIEST || cls == CLASS_PALADIN || cls == CLASS_SHAMAN)
                return false;
        }
    }
    return RebirthTrigger::IsActive();
}
