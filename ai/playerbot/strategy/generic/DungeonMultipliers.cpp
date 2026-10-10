#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/McGarrShazzrahPolicy.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "DungeonMultipliers.h"
#include "playerbot/strategy/actions/DungeonActions.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"

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

float GarrAoeOffMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    // Cheap exit: no Garr fight, no suppression.
    if (!ai->HasStrategy("garr", BotState::BOT_STATE_COMBAT))
        return 1.0f;
    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid>& attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    bool garrAlive = false;
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker && attacker->GetEntry() == kGarrEntry)
        {
            garrAlive = true;
            break;
        }
    }
    if (!garrAlive)
        return 1.0f;
    // DPS bots only: tanks and healers keep their (single-target) work.
    // Our role API exposes tank/heal/ranged; DPS = neither tank nor heal.
    Player* bot = ai->GetBot();
    bool botIsDps = !ai->IsTank(bot) && !ai->IsHeal(bot);
    // Name-matched AoE set (donor's explicit list): threat flags do not
    // mark our real AoE (Whirlwind etc. return SINGLE/NONE) and wrongly
    // flag heals plus single-target dots as AOE, so type/threat matching
    // is both under- and over-inclusive here.
    bool actionIsAoe = IsGarrSuppressedAoeAction(action->getName());
    if (ShouldSuppressGarrAoe(garrAlive, botIsDps, actionIsAoe))
        return 0.0f;
    return 1.0f;
}
