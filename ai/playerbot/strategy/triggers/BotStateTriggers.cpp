
#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "BotStateTriggers.h"

using namespace ai;

bool CombatStartTrigger::IsActive()
{
    if (!ai->IsStateActive(BotState::BOT_STATE_COMBAT) && !ai->IsStateActive(BotState::BOT_STATE_DEAD))
    {
        // Check if any member of the group (near this bot) is getting attacked
        return AI_VALUE(bool, "has attackers");
    }

    return false;
}

bool CombatEndTrigger::IsActive()
{
    // Check if the bot is currently in combat
    if (ai->IsStateActive(BotState::BOT_STATE_COMBAT))
    {
        // Don't end combat if pull is in progress
        PullStrategy* strategy = PullStrategy::Get(ai);
        if (strategy && strategy->HasTarget())
            return false;

        // Don't end combat if current target is alive and hostile
        Unit* currentTarget = AI_VALUE(Unit*, "current target");
        if (currentTarget && sServerFacade.IsAlive(currentTarget) && sServerFacade.IsHostileTo(currentTarget, bot))
            return false;

        // Don't end combat if explicit attack target is set and alive
        ObjectGuid attackGuid = AI_VALUE(ObjectGuid, "attack target");
        if (!attackGuid.IsEmpty())
        {
            Unit* attackTarget = ai->GetUnit(attackGuid);
            if (attackTarget && sServerFacade.IsAlive(attackTarget) && sServerFacade.IsHostileTo(attackTarget, bot))
                return false;
        }

        // Check if any member of the group (near this bot) is getting attacked
        return !AI_VALUE(bool, "has attackers");
    }

    return false;
}

bool DeathTrigger::IsActive()
{
    return !ai->IsStateActive(BotState::BOT_STATE_DEAD) && !sServerFacade.IsAlive(bot);
}

bool ResurrectTrigger::IsActive()
{
    return ai->IsStateActive(BotState::BOT_STATE_DEAD) && sServerFacade.IsAlive(bot);
}