#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "DungeonMultipliers.h"
#include "playerbot/strategy/actions/DungeonActions.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"
#include "playerbot/GeddonInfernoPolicy.h"

using namespace ai;

float PreventMoveAwayFromCreatureOnReachToCastMultiplier::GetValue(Action* action)
{
    MoveAwayFromCreature* moveAwayAction = dynamic_cast<MoveAwayFromCreature*>(action);
    if (moveAwayAction)
    {
        const Action* lastExecutedAction = ai->GetLastExecutedAction(BotState::BOT_STATE_COMBAT);
        if (lastExecutedAction)
        {
            const ReachTargetAction* reachAction = dynamic_cast<const ReachTargetAction*>(lastExecutedAction);
            if (reachAction && !reachAction->GetSpellName().empty())
            {
                return 0.0f;
            }
        }
    }

    return 1.0f;
}

float GeddonInfernoMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    // Cheap checks first: no aura lookups unless a Geddon fight is live.
    bool bombOnSelf = bot->HasAura(kLivingBombSpellId);
    if (!bombOnSelf && !ai->HasStrategy("geddon", BotState::BOT_STATE_COMBAT))
        return 1.0f;

    bool infernoActive = false;
    if (ai->HasStrategy("geddon", BotState::BOT_STATE_COMBAT))
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
        for (const ObjectGuid& attackerGuid : attackers)
        {
            Unit* attacker = ai->GetUnit(attackerGuid);
            if (attacker && attacker->GetEntry() == kGeddonEntry &&
                ai->HasAura(kInfernoSpellId, attacker))
            {
                infernoActive = true;
                break;
            }
        }
    }

    if (ShouldBlockGeddonMove(action->getName(), infernoActive, bombOnSelf))
        return 0.0f;

    return 1.0f;
}
