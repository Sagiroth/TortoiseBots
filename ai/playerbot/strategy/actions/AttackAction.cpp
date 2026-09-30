
#include "playerbot/playerbot.h"
#include "AttackAction.h"
#include "Movement/MovementGenerator.h"
#include "AI/CreatureAI.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/BotDiagnostics.h" // SC_LOG for attack-command diagnostic trace
#include "playerbot/strategy/generic/CombatStrategy.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"

using namespace ai;

namespace
{
void CommitExplicitAttackTarget(PlayerbotAI* ai, ObjectGuid guid)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    context->GetValue<ObjectGuid>("attack target")->Set(guid);
    context->GetValue<ObjectGuid>("explicit attack target")->Set(guid);

    // Target and attacker values may have been evaluated earlier in this same
    // second while the bot was following.  Invalidate them now so the first
    // combat decision sees the command target instead of clearing it as stale.
    context->GetValue<bool>("invalid target", "current target")->Reset();
    context->GetValue<std::list<ObjectGuid>>("attackers")->Reset();
    context->GetValue<std::list<ObjectGuid>>("attackers", 1)->Reset();
    context->GetValue<std::list<ObjectGuid>>("possible attack targets")->Reset();
    context->GetValue<Unit*>("dps target")->Reset();
    context->GetValue<Unit*>("tank target")->Reset();
    context->GetValue<bool>("has attackers")->Reset();
}
}

bool AttackAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();

    Unit* target = GetTarget();
    if (target && target->IsInWorld() && target->GetMapId() == bot->GetMapId())
    {
        return Attack(requester, target);
    }

    return false;
}

bool AttackMyTargetAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    SC_LOG("attack-cmd entry bot=%s requester=%s eventOwner=%s",
           bot ? bot->GetName() : "(null)",
           requester ? requester->GetName() : "(null)",
           event.GetOwner() ? event.GetOwner()->GetName() : "(null)");

    // A new explicit command supersedes any previous command-only priority.
    // Autonomous AttackAction callers do not touch this marker.
    context->GetValue<ObjectGuid>("explicit attack target")->Set(ObjectGuid());

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
                CommitExplicitAttackTarget(ai, guid);
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
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    Unit* rtiTarget = AI_VALUE(Unit*, "rti target");
    context->GetValue<ObjectGuid>("explicit attack target")->Set(ObjectGuid());

    if (rtiTarget && rtiTarget->IsInWorld() && rtiTarget->GetMapId() == bot->GetMapId())
    {
        if (Attack(requester, rtiTarget))
        {
            CommitExplicitAttackTarget(ai, rtiTarget->getObjectGuid());
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
    return true;
}

bool AttackRTITargetAction::isUseful()
{
    return true;
}

bool AttackAction::CanPetAttack(PlayerbotAI* ai, Pet* pet, Unit* target)
{
    if (!ai || !pet || !pet->IsAlive())
        return false;

    if (!target || !target->IsAlive())
        return false;

    Player* bot = ai->GetBot();
    if (!bot || !bot->IsValidAttackTarget(target))
        return false;

    // Don't send the pet to attack if waiting for attack
    if (WaitForAttackStrategy::ShouldWait(ai))
        return false;

    // Don't send the pet to attack if staying and target is outside spell range
    if (ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) &&
        bot->GetDistance(target) >= ai->GetRange("spell"))
        return false;

    // Don't send the pet to attack if set to passive
    if (pet->GetReactState() == REACT_PASSIVE)
        return false;

    // Keep pet off CC'd targets unless RTI mark says to ignore it, plus damage immunity gate
    bool ccProtected = !PossibleAttackTargetsValue::HasIgnoreCCRti(target, bot) &&
        (PossibleAttackTargetsValue::HasBreakableCC(target, bot) ||
         PossibleAttackTargetsValue::HasUnBreakableCC(target, bot));
    if (ccProtected || PossibleAttackTargetsValue::IsImmuneToDamage(target, bot))
        return false;

    return true;
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
               target ? sServerFacade.getDistance2d(bot, target) : -1.0f);
        if (bot->IsMounted() && (sServerFacade.getDistance2d(bot, target) < 40.0f || bot->IsFlying()))
        {
            ai->Unmount();

            if (bot->IsFlying())
            {
                return true;
            }
        }

        ObjectGuid guid = target->getObjectGuid();
        bot->SetSelectionGuid(target->getObjectGuid());

        Unit* oldTarget = AI_VALUE(Unit*, "current target");
        if(oldTarget)
        {
            SET_AI_VALUE(Unit*, "old target", oldTarget);
        }

        SET_AI_VALUE(Unit*, "current target", target);
        AI_VALUE(LootObjectStack*, "available loot")->Add(guid);

        Pet* pet = bot->GetPet();
        if (pet)
        {
            if (pet->GetReactState() == REACT_PASSIVE && !ai->GetMaster())
            {
                pet->SetReactState(REACT_DEFENSIVE);
            }

            UnitAI* creatureAI = ((Creature*)pet)->AI();
            if (creatureAI && CanPetAttack(ai, pet, target))
            {
                creatureAI->AttackStart(target);
            }
        }

        const bool isWaitingForAttack = WaitForAttackStrategy::ShouldWait(ai);

        if (ai->CanMove() && !sServerFacade.isInFront(bot, target, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT))
        {
            sServerFacade.SetFacingTo(bot, target);
        }

        bool result = true;

        // Don't attack target if it is waiting for attack or in stealth
        if (!ai->HasStrategy("stealthed", BotState::BOT_STATE_COMBAT) && !isWaitingForAttack)
        {
            ai->PlayAttackEmote(1);
            // The second argument arms the auto-attack state (UNIT_STAT_MELEE_ATTACKING),
            // and in this core that state *is* the auto-attack: Player::Update only swings
            // while it is set. A bot the strategy set calls "ranged" but which has no
            // ranged weapon (pre-10 druid on Wrath, elemental shaman - no wand, no bow)
            // was therefore left with no auto-attack at all, however close the mob came:
            // measured over 99 min at level 1, druid 0.008 and shaman 0.013 kills per
            // attack order against 0.035-0.079 for warrior/paladin/rogue and 0.11-0.23 for
            // the nukers. "ranged" describes the talent spec, not what the bot attacks
            // with; healers keep the old, deliberate hands-off handling.
            const bool meleeEval = !ai->IsHeal(bot) &&
                (!ai->IsRanged(bot) || !bot->GetWeaponForAttack(RANGED_ATTACK, true, true));
            result = bot->Attack(target, meleeEval);
            SC_LOG("attack-cmd bot->Attack bot=%s tgt=%s result=%d meleeEval=%d",
                   bot ? bot->GetName() : "(null)",
                   target ? target->GetName() : "(null)",
                   (int)result,
                   (int)meleeEval);
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
    else if (target->IsCreature() && static_cast<Creature*>(target)->IsInEvadeMode())
    {
        // The core refuses to start an attack on an evading creature (Unit::Attack) and
        // drops every point of damage aimed at it, so an order to attack one must not
        // even arm the auto-attack. The target values are already evade-aware, but they
        // are cached: a creature that entered evade after the target was picked still
        // reached this point through AttackAnythingAction, which then left the bot
        // hunting a mob it could never damage - and left "current target"/"attack target"
        // pointing at it, because a failed Attack() never replaces them.
        if (verbose)
        {
            std::ostringstream msg;
            msg << target->GetName();
            msg << " is evading";
            ai->TellPlayerNoFacing(requester, msg.str());
        }

        return false;
    }
    else if (sServerFacade.getDistance2d(bot, target) > sPlayerbotAIConfig.sightDistance)
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
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    return Attack(requester, AI_VALUE(Unit*, "duel target"));
}
