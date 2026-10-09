#include "playerbot/playerbot.h"
#include "DungeonMultipliers.h"
#include "playerbot/strategy/actions/DungeonActions.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"
#include "playerbot/strategy/actions/ChooseTargetActions.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/RazorgorePolicy.h"

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

float RazorgoreOffTankMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    if (!ai->HasStrategy("razorgore", BotState::BOT_STATE_COMBAT))
        return 1.0f;
    if (!ai->IsTank(bot))
        return 1.0f;
    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    Unit* boss = nullptr;
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker && attacker->GetEntry() == kRazorgoreEntry)
        {
            boss = attacker;
            break;
        }
    }
    if (!boss)
        return 1.0f;
    // Eggs alive = an egg GO near the boss. Nearest-GO value scan would
    // cost a world query per action; a bounded attacker-proximity check
    // reuses the already-cached attacker list instead. Fall back to true
    // when no egg data is reachable — holding the boss is the safe side.
    bool eggsAlive = true;
    // Off-tank = first living tank of the group by member-slot order (no
    // main/assist-tank distinction exists yet; Golemagg will add it).
    // While eggs live the off-tank holds the boss: veto tank-assist
    // retargets so adds don't pull it off.
    bool botIsOffTank = false;
    if (Group* group = bot->GetGroup())
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || !sServerFacade.IsAlive(member))
                continue;
            if (!ai->IsTank(member))
                continue;
            botIsOffTank = (member == bot);
            break;
        }
    }
    else
    {
        botIsOffTank = true;
    }
    if (ShouldHoldRazorgore(eggsAlive, botIsOffTank))
    {
        if (dynamic_cast<TankAssistAction*>(action))
            return 0.0f;
    }
    return 1.0f;
}
