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
    // Only TankAssistAction is ever vetoed: skip every scan for the ~99%
    // of other actions (donor vetoes TankAssistAction only, too).
    if (dynamic_cast<TankAssistAction*>(action) == nullptr)
        return 1.0f;
    if (!ai->HasStrategy("razorgore", BotState::BOT_STATE_COMBAT))
        return 1.0f;
    if (!ai->IsTank(bot))
        return 1.0f;
    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid>& attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
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
    // Donor's victim guard (BWLMultipliers.cpp:32): the off-tank must still
    // ACQUIRE the boss — veto only once it holds something.
    if (bot->GetVictim() == nullptr)
        return 1.0f;
    // Real egg check (donor AreRazorgoreEggsAlive): cached "nearest game
    // objects" value, entry 177807. Veto lifts when the eggs die.
    bool eggsAlive = false;
    const std::list<ObjectGuid> nearestGos = AI_VALUE(std::list<ObjectGuid>, "nearest game objects");
    for (const ObjectGuid& goGuid : nearestGos)
    {
        GameObject* go = ai->GetGameObject(goGuid);
        if (go && go->GetEntry() == kBlackDragonEggEntry)
        {
            eggsAlive = true;
            break;
        }
    }
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
            // Same-map only: an out-of-instance tank must not win slot 0.
            if (member->GetMapId() != bot->GetMapId())
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
        return 0.0f;
    return 1.0f;
}
