
#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/policy/combat/CombatIdlePolicy.h"
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
        if (AI_VALUE(bool, "has attackers"))
            return false;

        // The order cleared with the switch: "attack target" may hold a guid
        // the map no longer resolves, and "current target" a corpse kept for
        // loot. Leaving either behind re-arms the latch above on the next
        // fight's first quiet tick. Cleared only once the core agrees there
        // is no fight and the idle window has passed, the way .combatstop
        // does; the normal path above keeps its instant switch.
        time_t const combatStart = AI_VALUE(time_t, "combat start time");
        uint32 const idleSeconds = combatStart ? uint32(time(0) - combatStart) : 0;
        if (CombatIdleExitDue(sServerFacade.IsInCombat(bot), false, false, idleSeconds))
        {
            AiObjectContext* context = ai->GetAiObjectContext();
            context->GetValue<ObjectGuid>("attack target")->Set(ObjectGuid());
            context->GetValue<ObjectGuid>("explicit attack target")->Set(ObjectGuid());
            context->GetValue<Unit*>("current target")->Set(nullptr);
            bot->AttackStop();
        }

        return true;
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