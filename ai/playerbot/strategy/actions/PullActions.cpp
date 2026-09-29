
#include "playerbot/playerbot.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "playerbot/strategy/values/AttackersValue.h"
#include "PullActions.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "../../runtime/BotManager.h"
#include "../../runtime/PlayerbotAIStorage.h"

using namespace ai;

namespace
{
// Re-stamp every held party bot's combat-start clock, optionally narrowing
// its wait window. Shared by the pull landing (once the cast lands) and the
// pull end (when a pullback's tank is back and takes the fight over).
void RestampPullParty(Player* tank, uint32 waitSeconds, bool narrowWindow)
{
    if (!tank)
        return;
    for (Player* member : LiveGroupMembers(tank->GetGroup()))
    {
        if (!member || member == tank || !TortoiseBots::BotManager::Instance().IsBot(member->GetObjectGuid()))
            continue;
        PlayerbotAI* memberAi = PlayerbotAIStorage::Instance().GetAI(member);
        if (!memberAi || !memberAi->GetAiObjectContext())
            continue;
        if (!memberAi->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT))
            continue;
        if (narrowWindow)
            memberAi->GetAiObjectContext()->GetValue<uint8>("wait for attack time")->Set(static_cast<uint8>(waitSeconds));
        memberAi->GetAiObjectContext()->GetValue<time_t>("combat start time")->Set(time(0));
    }
}
} // namespace

Unit* PullNearestTargetAction::FindPullTarget(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    Unit* best = nullptr;

    // Same ceiling PullRequestAction::Execute enforces, so the trigger cannot
    // offer a target the action will then refuse.
    float bestDistance = sPlayerbotAIConfig.reactDistance * 3;

    for (const ObjectGuid& guid : ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible targets")->Get())
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;

        // "possible targets" carries neutrals too, and a neutral is not a pull.
        if (!bot->IsHostileTo(unit))
            continue;

        // Somebody is already on it - that is no longer a pull, it is a join.
        if (unit->IsInCombat())
            continue;

        if (!AttackersValue::IsValid(unit, bot, nullptr, false))
            continue;

        const float distance = unit->GetDistance(bot);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = unit;
        }
    }

    return best;
}

Unit* PullNearestTargetAction::GetTarget(Event& event)
{
    return FindPullTarget(ai);
}

bool PullRequestAction::Execute(Event& event)
{
    PullStrategy* strategy = PullStrategy::Get(ai);
    if (!strategy)
    {
        return false;
    }

    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();

    Unit* target = GetTarget(event);
    if (!target)
    {
        ai->TellPlayerNoFacing(requester, "You have no target");
        return false;
    }

    const float maxPullDistance = sPlayerbotAIConfig.reactDistance * 3;
    const float distanceToPullTarget = target->GetDistance(ai->GetBot());
    if (distanceToPullTarget > maxPullDistance)
    {
        ai->TellPlayerNoFacing(requester, "The target is too far away");
        return false;
    }

    if (!AttackersValue::IsValid(target, bot, nullptr, false))
    {
        ai->TellPlayerNoFacing(requester, "The target can't be pulled");
        return false;
    }

    if (!strategy->CanDoPullAction(target))
    {
        std::ostringstream out; out << "Can't perform pull action '" << strategy->GetPullActionName() << "'";
        ai->TellPlayerNoFacing(requester, out.str());
        return false;
    }

    //Set position to return to after pulling.
    PositionMap& posMap = AI_VALUE(PositionMap&, "position");
    PositionEntry pullPosition = posMap["pull"];

    if (requester && requester->IsInWorld() && requester->GetMapId() == bot->GetMapId())
    {
        pullPosition.Set(requester->getPositionX(), requester->getPositionY(), requester->getPositionZ(), requester->GetMapId());
    }
    else
    {
        pullPosition.Set(bot->getPositionX(), bot->getPositionY(), bot->getPositionZ(), bot->GetMapId());
    }
    posMap["pull"] = pullPosition;

    strategy->RequestPull(target);

    // Force change combat state to have a faster reaction time
    ai->OnCombatStarted();

    return true;
}

Unit* PullMyTargetAction::GetTarget(Event& event)
{
    // Prefer the GUID snapshotted by the command (event object): the live
    // selection can change between command validation and this tick.
    ObjectGuid snapshot = event.getObject();
    if (!snapshot.IsEmpty())
    {
        if (Unit* snapshotted = ai->GetUnit(snapshot))
            return snapshotted;
    }

    Unit* target = nullptr;

    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    if (event.GetSource() == "attack anything")
    {
        ObjectGuid guid = event.getObject();
        target = ai->GetCreature(guid);
    }
    else if (requester)
    {
        target = ai->GetUnit(requester->GetSelectionGuid());
    }

    return target;
}


Unit* PullRTITargetAction::GetTarget(Event& event)
{
    return AI_VALUE(Unit*, "rti target");
}

bool PullStartAction::Execute(Event& event)
{
    bool result = false;
    PullStrategy* strategy = PullStrategy::Get(ai);
    if (strategy)
    {
        Unit* target = strategy->GetTarget();
        if (target)
        {
            if (strategy->GetPreActionName().empty())
                result = true;
            else
            {
                result = ai->DoSpecificAction(strategy->GetPreActionName(), event, true);
                if(result)
                    SetDuration(ai->GetAIInternalUpdateDelay());
            }

            // Set the pet on passive mode during the pull
            Pet* pet = bot->GetPet();
            if (pet)
            {
                UnitAI* creatureAI = ((Creature*)pet)->AI();
                if (creatureAI)
                {
                    strategy->SetPetReactState(pet->GetReactState());
                    pet->SetReactState(REACT_PASSIVE);
                }
            }

            strategy->OnPullStarted();
        }
    }

    return result;
}


PullAction::PullAction(PlayerbotAI* ai, std::string name) : CastSpellAction(ai, name)
{
    InitPullAction();
}

bool PullAction::Execute(Event& event)
{
    InitPullAction();

    PullStrategy* strategy = PullStrategy::Get(ai);
    if (strategy)
    {
        Unit* target = strategy->GetTarget();
        if (target)
        {
            // Check if we are on pull range. The "pull action" node also carries
            // "reach pull" as a prerequisite, so the queued path moves first;
            // this explicit branch covers the direct DoSpecificAction path used
            // by the .bot command, which bypasses prerequisites.
            const float distanceToTarget = target->GetDistance(bot);
            if (distanceToTarget > strategy->GetRange())
            {
                // Retry the reach pull action
                strategy->RequestPull(target, false);
                ai::Event reachEvent(event.GetSource(), "", event.GetOwner());
                ai->DoSpecificAction("reach pull", reachEvent, true);
                return false;
            }

            if (sServerFacade.isMoving(bot))
            {
                // Force stop
                ai->StopMoving();
                strategy->RequestPull(target, false);
                return false;
            }

            std::string actionName = strategy->GetPullActionName();

            // Execute the pull action. The AI's "current target" and the bot's
            // selection GUID must name the same unit: InvalidTargetValue reads
            // a mismatch as "invalid target", and the combat engine then wipes
            // the target and calls AttackStop() (the tank stood at the anchor
            // doing nothing while the mob beat on it). CastSpell restores the
            // pre-cast selection, so the selection is re-asserted after the
            // spell branch too.
            SET_AI_VALUE(Unit*, "current target", GetTarget());

            if (actionName == "reach pull")
            {
                // Body pull: the tank is in melee reach of the target, so the
                // pull has landed. Do not fail on the attack request itself -
                // a mounted tank cannot attack (Unit::Attack) and a repeated
                // request on the same victim returns false; failing here used
                // to leave the tank muted at the mob until the pull timeout.
                if (bot->IsMounted())
                    ai->Unmount();
                bot->Attack(target, true);
                bot->SetSelectionGuid(target->getObjectGuid());
                strategy->OnPullActionCompleted();
                return true;
            }
            else if (ai->DoSpecificAction(actionName, event, true))
            {
                bot->SetSelectionGuid(target->getObjectGuid());
                strategy->OnPullActionCompleted();
                // Anchor the DPS join delay to the landing, not to the
                // command: re-stamp every held party bot's combat-start clock
                // so an ordinary pull holds for the full delay after the cast
                // lands. Pullbacks keep the wide return-covering window here;
                // the pull end narrows it to the join delay.
                RestampPullParty(bot, 0, false);
                return true;
            }
            else
                return false;
        }
    }

    return false;
}

bool PullAction::isPossible()
{
    InitPullAction();

    PullStrategy* strategy = PullStrategy::Get(ai);
    if (strategy)
    {
        if (strategy->GetPullActionName() == "reach pull")
            return true;

        std::string spellName = strategy->GetSpellName();
        Unit* target = strategy->GetTarget();
        if (!spellName.empty() && target)
        {
            if (!ai->CanCastSpell(spellName, target, true, nullptr, true))
            {
                return false;
            }
        }
    }

    return true;
}

void PullAction::InitPullAction()
{
    // Get the pull action spell name from the strategy
    PullStrategy* strategy = PullStrategy::Get(ai);
    if (strategy)
    {
        std::string spellName = strategy->GetSpellName();
        if (!spellName.empty())
        {
            SetSpellName(spellName);

            float spellRange;
            if (ai->GetSpellRange(spellName, &spellRange))
            {
                range = spellRange;
            }
        }
    }
}

bool PullEndAction::Execute(Event& event)
{
    PullStrategy* strategy = PullStrategy::Get(ai);
    if (strategy)
    {
        // Restore the pet react state
        Pet* pet = bot->GetPet();
        if (pet)
        {
            UnitAI* creatureAI = ((Creature*)pet)->AI();
            if (creatureAI)
            {
                pet->SetReactState(strategy->GetPetReactState());
            }
        }

        // Per-command return mode: restore the tank's default before erasing
        // anything, so an ordinary pull re-arms the tank kit and a pullback
        // never leaks into the next command.
        bool wasCommand = strategy->IsCommandActive();
        bool wasPullback = strategy->IsCommandPullback();
        bool hadPullBack = strategy->HadPullBack();
        if (wasCommand && (wasPullback != hadPullBack))
        {
            ai->ChangeStrategy((hadPullBack ? "+" : "-") + std::string("pull back"),
                BotState::BOT_STATE_ALL);
        }

        // The tank is back and takes the fight over here: this is the moment a
        // pullback's join window counts from. Narrow every held party bot's
        // wait window to the join delay and restart its clock, so the DPS
        // arrive joinDelay after the tank is back instead of after the wide
        // return-covering window placed at the command.
        if (wasCommand && wasPullback)
            RestampPullParty(bot, strategy->GetCommandJoinDelay(), true);

        // Remove the saved pull position
        AiObjectContext* context = ai->GetAiObjectContext();

        // Hand the pulled mob over to the tank's own rotation. OnPullEnded
        // below drops the pull target, and a target that only lived there left
        // the tank idle: InvalidTargetValue reads an AI "current target" that
        // does not match the bot selection GUID as "invalid target", so the
        // combat engine wiped it and called AttackStop() instead of fighting.
        // Keep the mob as the live target (selection, current target, attack
        // target) and the tank's normal rotation takes it from here - for a
        // pullback it does so at the anchor, where the mob comes to it.
        Unit* pullTarget = strategy->GetTarget();
        if (pullTarget && pullTarget->IsInWorld() && pullTarget->IsAlive() &&
            pullTarget->GetMapId() == bot->GetMapId() && bot->IsValidAttackTarget(pullTarget))
        {
            context->GetValue<ObjectGuid>("attack target")->Set(pullTarget->getObjectGuid());
            SET_AI_VALUE(Unit*, "current target", pullTarget);
            bot->SetSelectionGuid(pullTarget->getObjectGuid());
        }

        PositionMap& posMap = AI_VALUE(PositionMap&, "position");
        PositionEntry stayPosition = posMap["pull"];
        if (stayPosition.isSet())
        {
            // After a pullback the tank holds the anchor for the join window:
            // drop a stay at the anchor position so the tank fights the
            // incoming mob in the corner instead of drifting back out.
            if (wasCommand && wasPullback)
            {
                PositionEntry tankStay = posMap["stay"];
                tankStay.Set(stayPosition.x, stayPosition.y, stayPosition.z, stayPosition.mapId);
                posMap["stay"] = tankStay;
                ai->SetMovementStrategy("stay");
            }
            posMap.erase("pull");
        }
        // The held party releases itself per bot once its own join window
        // elapses ("pull hold expired" trigger): the tank must not drop the
        // wait window here, or the join delay would be zero.

        strategy->OnPullEnded();
        return true;
    }

    return false;
}

bool ReleasePullHoldAction::Execute(Event& event)
{
    (void)event;
    ai->ChangeStrategy("-wait for attack", BotState::BOT_STATE_ALL);
    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& posMap = AI_VALUE(PositionMap&, "position");
    PositionEntry holdPos = posMap["pull hold"];
    if (!holdPos.isSet())
        return true;
    PositionEntry stayPos = posMap["stay"];
    if (stayPos.isSet() && stayPos.mapId == holdPos.mapId &&
        stayPos.x == holdPos.x && stayPos.y == holdPos.y)
    {
        ai->SetMovementStrategy("follow");
        posMap.erase("stay");
    }
    posMap.erase("pull hold");
    return true;
}
