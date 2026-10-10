#include "playerbot/playerbot.h"
#include "SapphironDungeonTriggers.h"
#include "playerbot/strategy/SapphironDungeonHelper.h"

using namespace ai;

bool SapphironAirTrigger::IsActive()
{
    Unit* boss = FindSapphironBoss(ai, bot);
    if (!boss)
        return false;
    return boss->IsHovering();
}

bool SapphironBlizzardTrigger::IsActive()
{
    return ai->HasAura(28547, bot);
}
