
#include "playerbot/playerbot.h"
#include "AttackAction.h"
#include "Movement/MovementGenerator.h"
#include "AI/CreatureAI.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/BotDiagnostics.h" // SC_LOG for attack-command diagnostic trace
#include "playerbot/strategy/generic/CombatStrategy.h"

using namespace ai;

bool AttackAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    Unit* target = GetTarget();
    if (target && target->IsInWorld() && target->GetMapId() == bot->GetMapId())
    {
        return Attack(requester, target);
    }

    return false;
}

bool AttackMyTargetAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    SC_LOG("attack-cmd entry bot=%s requester=%s eventOwner=%s",
           bot ? bot->GetName() : "(null)",
           requester ? requester->GetName() : "(null)",
           event.getOwner() ? event.getOwner()->GetName() : "(null)");

    if(requester)
    {
        const ObjectGuid guid = requester->GetSelectionGuid();
        SC_LOG("attack-cmd selection bot=%s requester=%s selGuid=0x%016llx",
               bot ? bot->GetName() : "(null)",
               requester->GetName(),
               (unsigned long long)guid.GetRawValue());

        if (guid)
        {
            Unit* tgt = ai->GetUnit(guid);
            SC_LOG("attack-cmd target bot=%s tgt=%s tgtMap=%d botMap=%u",
                   bot ? bot->GetName() : "(null)",
                   tgt ? tgt->GetName() : "(null-unit)",
                   tgt ? (int)tgt->GetMapId() : -1,
                   bot ? bot->GetMapId() : 0);

            if (Attack(requester, tgt))
            {
                SET_AI_VALUE(ObjectGuid, "attack target", guid);
                SC_LOG("attack-cmd OK bot=%s tgt=%s",
                       bot ? bot->GetName() : "(null)",
                       tgt ? tgt->GetName() : "(null)");
                return true;
            }
            SC_LOG("attack-cmd FAIL bot=%s — Attack() returned false", bot ? bot->GetName() : "(null)");
        }
        else if (verbose)
        {
            SC_LOG("attack-cmd FAIL bot=%s — requester has no selection", bot ? bot->GetName() : "(null)");
            ai->TellError(requester, "You have no target");
        }
    }
    else
    {
        SC_LOG("attack-cmd FAIL bot=%s — no requester (event has no owner, no master)", bot ? bot->GetName() : "(null)");
    }

    return false;
}

bool AttackRTITargetAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    Unit* rtiTarget = AI_VALUE(Unit*, "rti target");

    if (rtiTarget && rtiTarget->IsInWorld() && rtiTarget->GetMapId() == bot->GetMapId())
    {
        if (Attack(requester, rtiTarget))
        {
            SET_AI_VALUE(ObjectGuid, "attack target", rtiTarget->GetObjectGuid());
            return true;
        }
    }
    else
    {
        ai->TellError(requester, "I dont see my rti attack target");
    }

    return false;
}

bool AttackMyTargetAction::isUseful()
{
    return !ai->ContainsStrategy(STRATEGY_TYPE_HEAL) || ai->HasStrategy("offdps", BotState::BOT_STATE_COMBAT);
}

bool AttackRTITargetAction::isUseful()
{
    return !ai->ContainsStrategy(STRATEGY_TYPE_HEAL) || ai->HasStrategy("offdps", BotState::BOT_STATE_COMBAT);
}

bool AttackAction::Attack(Player* requester, Unit* target)
{
    MotionMaster &mm = *bot->GetMotionMaster();
	if (mm.GetCurrentMovementGeneratorType() == TAXI_MOTION_TYPE || (bot->IsFlying() && WorldPosition(bot).currentHeight() > 10.0f))
    {
        SC_LOG("attack-cmd FAIL bot=%s — taxi/flying", bot ? bot->GetName() : "(null)");
        if (verbose)
        {
            ai->TellPlayerNoFacing(requester, "I cannot attack in flight");
        }

        return false;
    }

    if (IsTargetValid(requester, target))
    {
        SC_LOG("attack-cmd valid-tgt bot=%s tgt=%s mounted=%d range=%.1f",
               bot ? bot->GetName() : "(null)",
               target ? target->GetName() : "(null)",
               bot ? (int)bot->IsMounted() : -1,
               target ? sServerFacade.GetDistance2d(bot, target) : -1.0f);
        if (bot->IsMounted() && (sServerFacade.GetDistance2d(bot, target) < 40.0f || bot->IsFlying()))
        {
            ai->Unmount();
            
            if (bot->IsFlying())
            {
                return true;
            }
        }

        ObjectGuid guid = target->GetObjectGuid();
        bot->SetSelectionGuid(target->GetObjectGuid());

        Unit* oldTarget = AI_VALUE(Unit*, "current target");
        if(oldTarget)
        {
            SET_AI_VALUE(Unit*, "old target", oldTarget);
        }

        SET_AI_VALUE(Unit*, "current target", target);
        AI_VALUE(LootObjectStack*, "available loot")->Add(guid);

        const bool isWaitingForAttack = WaitForAttackStrategy::ShouldWait(ai);
        Pet* pet = bot->GetPet();
        if (pet)
        {
            UnitAI* creatureAI = ((Creature*)pet)->AI();
            if (creatureAI)
            {
                // Don't send the pet to attack if the bot is waiting for attack
                if (!isWaitingForAttack && (!ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) || AI_VALUE2(float, "distance", "current target") < ai->GetRange("spell")))
                {
                    // Reset the pet state if no master
                    if (creatureAI->GetReactState() == REACT_PASSIVE && !ai->GetMaster())
                    {
                        creatureAI->SetReactState(REACT_DEFENSIVE);
                    }

                    // Don't send the pet to attack if set to passive
                    if (creatureAI->GetReactState() != REACT_PASSIVE)
                    {
                        creatureAI->AttackStart(target);
                    }
                }
            }
        }

        if (ai->CanMove() && !sServerFacade.IsInFront(bot, target, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT))
        {
            sServerFacade.SetFacingTo(bot, target);
        }

        bool result = true;

        // Don't attack target if it is waiting for attack or in stealth
        if (!ai->HasStrategy("stealthed", BotState::BOT_STATE_COMBAT) && !isWaitingForAttack)
        {
            ai->PlayAttackEmote(1);
            result = bot->Attack(target, !ai->IsRanged(bot) || (sServerFacade.GetDistance2d(bot, target) < 5.0f));
            SC_LOG("attack-cmd bot->Attack bot=%s tgt=%s result=%d",
                   bot ? bot->GetName() : "(null)",
                   target ? target->GetName() : "(null)",
                   (int)result);
        }
        else
        {
            SC_LOG("attack-cmd skip bot->Attack bot=%s — stealthed=%d isWaitingForAttack=%d",
                   bot ? bot->GetName() : "(null)",
                   (int)ai->HasStrategy("stealthed", BotState::BOT_STATE_COMBAT),
                   (int)isWaitingForAttack);
        }

        if (result)
        {
            // Force change combat state to have a faster reaction time
            ai->OnCombatStarted();
        }

        return result;
    }

    SC_LOG("attack-cmd FAIL bot=%s — IsTargetValid rejected", bot ? bot->GetName() : "(null)");
    return false;
}

bool AttackAction::IsTargetValid(Player* requester, Unit* target)
{
    if (!target)
    {
        if (verbose) 
        {
            ai->TellPlayerNoFacing(requester, "I have no target");
        }

        return false;
    }
    else if (sServerFacade.IsFriendlyTo(bot, target))
    {
        if (verbose)
        {
            std::ostringstream msg;
            msg << target->GetName();
            msg << " is friendly to me";
            ai->TellPlayerNoFacing(requester, msg.str());
        }

        return false;
    }
    else if (sServerFacade.UnitIsDead(target))
    {
        if (verbose)
        {
            std::ostringstream msg;
            msg << target->GetName();
            msg << " is dead";
            ai->TellPlayerNoFacing(requester, msg.str());
        }

        return false;
    }
    else if (sServerFacade.GetDistance2d(bot, target) > sPlayerbotAIConfig.sightDistance)
    {
        if (verbose)
        {
            std::ostringstream msg;
            msg << target->GetName();
            msg << " is too far away";
            ai->TellPlayerNoFacing(requester, msg.str());
        }

        return false;
    }

    return true;
}

bool AttackDuelOpponentAction::isUseful()
{
    return AI_VALUE(Unit*, "duel target");
}

bool AttackDuelOpponentAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    return Attack(requester, AI_VALUE(Unit*, "duel target"));
}
