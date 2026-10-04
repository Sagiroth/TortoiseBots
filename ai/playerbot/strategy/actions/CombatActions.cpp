
#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "CombatActions.h"
#include "HunterRangedTelemetry.h"

using namespace ai;

bool SwitchToMeleeAction::isUseful()
{
    return ai->HasStrategy("ranged", BotState::BOT_STATE_COMBAT);
}

bool SwitchToMeleeAction::Execute(Event &event)
{
    if (Unit* target = AI_VALUE(Unit*, "current target"))
    {
        // Hunter ranged telemetry: the pre-switch kit is what the hunter is
        // about to lose, plus the exact branch that fired it (see
        // HunterRangedTelemetry.h).
        LogHunterRangedEvent(ai, "SwitchToMelee", HunterSwitchWhy(ai, target, false));
        bot->Attack(target, true);
        return ChangeCombatStrategyAction::Execute(event);
    }
    return false;
}

bool SwitchToRangedAction::isUseful()
{

    return ai->HasStrategy("close", BotState::BOT_STATE_COMBAT);
}

bool SwitchToRangedAction::Execute(Event &event)
{
    if (Unit* target = AI_VALUE(Unit*, "current target"))
    {
        LogHunterRangedEvent(ai, "SwitchToRanged", HunterSwitchWhy(ai, target, true));
        bot->AttackStop(true);
        return ChangeCombatStrategyAction::Execute(event);
    }
    return false;
}
