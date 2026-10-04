#include "playerbot/strategy/Action.h"
#include "ChooseTargetActions.h"
#include "Movement/MovementGenerator.h"
#include "AI/CreatureAI.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "playerbot/PullRegenPolicy.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include <map>

bool DpsAssistAction::isUseful()
{
    // if carry flag, do not start fight
    if (bot->HasAura(23333) || bot->HasAura(23335))
        return false;

    return true;
}

bool AttackAnythingAction::isUseful()
{
    if (!bot->IsAlive()) // a ghost travelling or picking fights instead of going for its corpse
        return false;

    if (!ai->AllowActivity(GRIND_ACTIVITY)) //Bot not allowed to be active
        return false;

    if (ai->HasRealPlayerMaster())
        return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    Unit* target = GetTarget();

    if (!target || !ai->IsSafe(target))
        return false;

    if (ai->ContainsStrategy(STRATEGY_TYPE_HEAL) && !ai->HasStrategy("offdps", BotState::BOT_STATE_COMBAT))
        return false;

    // Revenge first: a mob already fighting the bot (victim set) is always
    // answered, even wounded and even while travelling - this is self-defence
    // on every trip, including a completed hand-in walk. Like the donor (whose
    // AttackAnythingAction has no facing gate), there is no isInFront check: a
    // mob that aggros from the side or behind while the bot walks on never
    // stands in front, and requiring it left travelling bots dying without
    // fighting back (quest-taker trips: 5% fought back within 90 s vs 60-93%
    // elsewhere, Oct 2026 pool). Answering an attacker that already holds the
    // bot as victim pulls nothing new, so there is no adds risk.
    if (!target->IsPlayer() && target->IsHostileTo(bot) &&
        target->GetVictim() == bot)
        return true;

    // A finished quest waiting at its taker is the bot's own business: the walk
    // to the hand-in must not lose to "attack before being attacked", or a bot
    // with a level-appropriate mob in front of it never takes a step. Scoped to
    // that one case - an objective, grind spot, vendor or giver target keeps the
    // old attack-first rule - so grinding is not starved for bots that merely
    // happen to be travelling.
    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
    TravelDestination* destination = travelTarget->IsActive() ? travelTarget->GetDestination() : nullptr;
    if (destination && destination->GetPurpose() == TravelDestinationPurpose::QuestTaker)
    {
        // QuestTaker destinations are only ever built as
        // QuestRelationTravelDestination (TravelMgr::AddDestination), whose id
        // is the quest that taker rewards.
        QuestTravelDestination* questDestination = static_cast<QuestTravelDestination*>(destination);
        if (bot->GetQuestStatus(questDestination->GetQuestId()) == QUEST_STATUS_COMPLETE &&
            CanFreeMoveValue::CanFreeMoveTo(ai, *travelTarget->getPosition()))
            return false;
    }
    // A wounded pool bot sits out a NEW pull until it eats/drinks back above
    // mediumHealth (mana users: mediumMana too). Revenge above already
    // answered, so the gate below only runs with no attacker holding the bot.
    // Pool-only (masterless random): owned bots and bots with a real
    // player master keep today's behaviour.
    if (!bot->InBattleGround() && sRandomBotFacade.IsRandomBot(bot) && !ai->HasRealPlayerMaster() &&
        bot->GetAttackers().empty())
    {
        uint8 const healthPct = AI_VALUE2(uint8, "health", "self target");
        bool const hasMana = AI_VALUE2(bool, "has mana", "self target");
        uint8 const manaPct = hasMana ? AI_VALUE2(uint8, "mana", "self target") : 100;
        if (ai::ShouldDeferGrindPull(healthPct, hasMana, manaPct,
            sPlayerbotAIConfig.mediumHealth, sPlayerbotAIConfig.mediumMana))
            return false;
    }

    // The pre-emptive "attack before being attacked" strike while travelling
    // only starts a fresh pull when no possible adds lurk nearby and the mob
    // is inside the grind level cap. It keeps the front-arc check: starting a
    // fight is only for what the bot walks into, never for side aggro (that
    // is revenge above once it lands, or walked past otherwise).
    if (!target->IsPlayer() && sServerFacade.isInFront(bot, target, target->GetCombatReach(bot, false, 0.0f) * 1.5f, M_PI_F * 0.5f) && target->IsHostileTo(bot) &&
        ai::AllowPreemptiveStrike((int)target->GetLevel(), bot->GetLevel(), ai->HasRealPlayerMaster(), AI_VALUE(bool, "possible adds")))
        return true;

    if (AI_VALUE(bool, "travel target traveling") && CanFreeMoveValue::CanFreeMoveTo(ai, *travelTarget->getPosition())) //Bot is traveling
        return false;

    return true;
}

bool ai::AttackAnythingAction::isPossible()
{
    return AttackAction::isPossible() && GetTarget();
}

bool ai::AttackAnythingAction::Execute(Event& event)
{
    bool result = AttackAction::Execute(event);
    if (result)
    {
        Unit* grindTarget = GetTarget();
        // The grind pick is cached: a mob the core flagged unreachable after the pick
        // still gets ordered here. Refuse it before "current target" arms, so the bot
        // never holds a ghost it cannot path to.
        if (grindTarget && grindTarget->IsCreature() &&
            static_cast<Creature*>(grindTarget)->IsEvadeBecauseTargetNotReachable())
        {
            return false;
        }
        if (grindTarget)
        {
            std::string grindName = grindTarget->GetName();
            if (!grindName.empty())
            {
                sPlayerbotAIConfig.logEvent(ai, "AttackAnythingAction", grindName + " (lvl " + std::to_string(grindTarget->GetLevel()) + ")", std::to_string(grindTarget->GetEntry()));
                LogRepeatOrder(grindTarget);

                // The order above was the last one worth giving this creature.
                if (GiveUpOnGrindTarget(grindTarget))
                    return true;

                if (ai->HasStrategy("pull", BotState::BOT_STATE_COMBAT))
                {
                    if (PullStrategy* strategy = PullStrategy::Get(ai))
                    {
                        if (strategy->CanDoPullAction(grindTarget) && (ai->GetBot()->GetClass() == CLASS_DRUID || ai->GetBot()->GetClass() == CLASS_PALADIN || AI_VALUE2(uint32, "item count", "ammo")))
                        {
                            Event pullEvent("attack anything", grindTarget->getObjectGuid());
                            bool doAction = ai->DoSpecificAction("pull my target", pullEvent, true);

                            if (doAction)
                            {
                                return true;
                            }
                        }
                    }
                }

                // This is autonomous/grind state, not a human command. Do
                // not let it inherit an old command-only priority marker.
                context->GetValue<ObjectGuid>("explicit attack target")->Set(ObjectGuid());
                context->GetValue<ObjectGuid>("attack target")->Set(grindTarget->getObjectGuid());

                // The order has to carry the walk to the target with it. A grind target is
                // picked from up to sightDistance (60 yd) away, and the only other thing that
                // closes that distance is the combat engine's reach action - which does not
                // reliably run: of 395 live bots watched for 150 s at 1 Hz, none of the 207
                // that ended the window where they started ever executed one, while 22 reach
                // melee and 25 reach spell samples belong to the 188 that did move. Those 207
                // spent the window on the stuck reset, "select new target" or nothing at all -
                // standing still 20-60 yd from a mob they had just ordered. Donor
                // mod-playerbots keeps its StopMoving commented out here for the same reason:
                // the order must not itself break the approach. So close the distance here,
                // where the order is given, and stand still only when already inside attack
                // range. The reach action brings its own guards: it is a no-op in range, it
                // gives up on a target it cannot close on for 15 s - blacklisting the creature,
                // and on a second give-up the whole spot - and it never moves a bot that has
                // "stay". Kit choice mirrors the combat engine: the ranged kit holds casting
                // distance, everything else fights in melee (which is also what a kit with
                // neither carries as its default action: the below-10 leveling druid).
                const bool rangedKit = ai->HasStrategy("ranged", BotState::BOT_STATE_COMBAT) &&
                    !ai->HasStrategy("close", BotState::BOT_STATE_COMBAT);
                if (ai->DoSpecificAction(rangedKit ? "reach spell" : "reach melee", event, true))
                {
                    // The nested reach has booked its own walk wait (ReachTargetAction::Execute
                    // -> WaitForReach -> SetDuration -> Engine::ListenAndExecute ->
                    // SetActionDuration), but the engine applies the *outer* action's duration
                    // once Execute() returns - and this action's own duration is the reactDelay
                    // default. Without this the 100 ms would land on top of the reach's wait
                    // (up to MaxWaitForMove), the bot would tick straight away again, the combat
                    // engine would re-run the reach and relaunch the spline once per tick for the
                    // whole walk. Carry the wait the reach asked for as this action's duration.
                    SetDuration(ai->GetAIInternalUpdateDelay());
                }
                else
                {
                    ai->StopMoving();
                    // No movement was taken, so this action must not keep a wait from an
                    // earlier walk (the action object is cached per context): release the
                    // default so the next order is considered on the next tick.
                    SetDuration(sPlayerbotAIConfig.reactDelay);
                }
            }
        }
    }

    return result;
}

void ai::AttackAnythingAction::LogRepeatOrder(Unit* target)
{
    // A second order on the same mob inside this window is a repeat: a bot that killed a
    // mob and moved on does not come back to the same creature in under a minute.
    uint32 const repeatWindowMs = 60 * IN_MILLISECONDS;
    // Logging starts at the third repeat (a single repeat is normal - target re-selected
    // after a reset) and then at most one row per window.
    uint32 const minRepeatsToLog = 3;

    uint32 const nowMs = WorldTimer::getMSTime();
    bool const repeat = lastGrindOrderMs && lastGrindTarget == target->getObjectGuid() &&
        WorldTimer::getMSTimeDiff(lastGrindOrderMs, nowMs) < repeatWindowMs;

    lastGrindTarget = target->getObjectGuid();
    lastGrindOrderMs = nowMs;
    grindRepeatCount = repeat ? grindRepeatCount + 1 : 0;

    if (grindRepeatCount < minRepeatsToLog)
        return;

    if (lastRepeatLogMs && WorldTimer::getMSTimeDiff(lastRepeatLogMs, nowMs) < repeatWindowMs)
        return;

    lastRepeatLogMs = nowMs;

    std::ostringstream out;
    out << "guid=" << target->getObjectGuid().GetRawValue();
    out << " entry=" << target->GetEntry();
    out << " dz=" << (int)(bot->GetPositionZ() - target->GetPositionZ());
    out << " dist=" << (int)sServerFacade.getDistance2d(bot, target);
    out << " incombat=" << (int)bot->IsInCombat();
    out << " victim=" << (int)(target->GetVictim() == bot);
    out << " repeats=" << grindRepeatCount;
    sPlayerbotAIConfig.logEvent(ai, "GrindTargetRepeat", out.str(), std::to_string(target->GetEntry()));
}

// The telemetry above separates the two shapes of a repeated order, and live data
// showed the dominant one: 67 of 500 fresh bots frozen at a single coordinate for
// 30+ minutes, ordering the same creature 27 times an hour with repeats=3..14,
// incombat=0, victim=0 and 10-59 yd still to go - no kill, no loot, no travel move
// (report-stationary.md). ReachTargetAction has the right remedy - 15 s without
// headway puts the creature on the "unreachable targets"/"unreachable entries"
// lists for five minutes, which also drops the grind destination
// (GrindTravelDestination::IsActive) so the bot walks somewhere else - but its
// window restarts whenever the creature wanders two yards closer or the order
// moves to another creature of the same kind, so on a field of wandering mobs it
// can go an hour without ever firing.
//
// So run the same rule on the order record instead, where the bot's intent is
// known: kGrindGiveUpOrders orders of the same creature, spread over at least
// kGrindGiveUpMinSpanMs, with the bot never in that fight and no order ever seeing
// the creature closer than 2 yd under the best distance so far, means every route
// the picker offers ends in the same standstill. Blacklist exactly what the reach
// action would have, so the existing escape paths do the rest.
static uint32 const kGrindGiveUpOrders = 5;
static uint32 const kGrindGiveUpMinSpanMs = 60 * IN_MILLISECONDS;

// Two stranded creatures of one kind this close together is a bad camp, not bad luck; the reach
// action uses the same three minutes for its spot rule.
static uint32 const kGrindGiveUpEntryWindowMs = 3 * MINUTE * IN_MILLISECONDS;

bool ai::AttackAnythingAction::GiveUpOnGrindTarget(Unit* target)
{
    if (!target || !target->IsCreature())
        return false;

    // A bot that is in the fight - or that the creature is fighting - is
    // reachable by definition, and a target that keeps coming after it is
    // handled by the reach action's own exception.
    if (bot->IsInCombat() || target->GetVictim() == bot)
    {
        grindUnreachableTarget = ObjectGuid();
        grindUnreachableSinceMs = 0;
        grindUnreachableOrders = 0;
        return false;
    }

    uint32 const nowMs = WorldTimer::getMSTime();
    float const distance = sServerFacade.getDistance2d(bot, target);

    if (grindUnreachableTarget != target->getObjectGuid() || grindUnreachableSinceMs == 0)
    {
        grindUnreachableTarget = target->getObjectGuid();
        grindUnreachableSinceMs = nowMs;
        grindUnreachableBestDist = distance;
        grindUnreachableOrders = 1;
        return false;
    }

    ++grindUnreachableOrders;

    // Any order that finds the creature closer than the best distance so far is
    // the bot actually closing in - the mob walking to us counts, exactly as the
    // reach action counts it.
    if (distance < grindUnreachableBestDist - 2.0f)
    {
        grindUnreachableBestDist = distance;
        grindUnreachableOrders = 1;
        return false;
    }

    if (grindUnreachableOrders < kGrindGiveUpOrders ||
        WorldTimer::getMSTimeDiff(grindUnreachableSinceMs, nowMs) < kGrindGiveUpMinSpanMs)
        return false;

    uint32 const expiresAt = nowMs + 5 * MINUTE * IN_MILLISECONDS;
    context->GetValue<std::map<ObjectGuid, uint32>&>("unreachable targets")->Get()[grindUnreachableTarget] = expiresAt;

    // Never the whole kind on one creature. "unreachable entries" ignores every creature of the
    // entry in target selection and drops the entry as a grind destination, so one thug stuck
    // behind the abbey fence would hide the entire species for five minutes - quest targets
    // included. A second, different creature of the same kind stranded inside the window is
    // what makes the kind itself suspect; then set it aside, exactly as the reach action does
    // on its second give-up in three minutes.
    uint32 const entry = target->GetEntry();
    if (grindGiveUpEntry == entry && grindGiveUpEntryTarget != grindUnreachableTarget &&
        WorldTimer::getMSTimeDiff(grindGiveUpEntryMs, nowMs) <= kGrindGiveUpEntryWindowMs)
    {
        context->GetValue<std::map<uint32, uint32>&>("unreachable entries")->Get()[entry] = expiresAt;
        grindGiveUpEntry = 0;
        grindGiveUpEntryTarget = ObjectGuid();
        grindGiveUpEntryMs = 0;
    }
    else
    {
        grindGiveUpEntry = entry;
        grindGiveUpEntryTarget = grindUnreachableTarget;
        grindGiveUpEntryMs = nowMs;
    }

    // Drop the order with the creature so the next pick starts from a clean
    // slate instead of re-arming the target we just blacklisted. CurrentTargetValue
    // resolves from its own selection guid, so it has to be Set() to null - Reset()
    // only clears the base value it never reads.
    context->GetValue<ObjectGuid>("attack target")->Set(ObjectGuid());
    context->GetValue<ObjectGuid>("explicit attack target")->Set(ObjectGuid());
    context->GetValue<Unit*>("current target")->Set(nullptr);
    bot->AttackStop();

    ai->TellDebug(ai->GetMaster(), "Giving up on " + std::string(target->GetName()) +
        " - ordered " + std::to_string(grindUnreachableOrders) + " times without ever reaching it", "debug move");

    grindUnreachableTarget = ObjectGuid();
    grindUnreachableSinceMs = 0;
    grindUnreachableOrders = 0;
    return true;
}

bool AttackEnemyPlayerAction::isUseful()
{
    return !sPlayerbotAIConfig.IsInPvpProhibitedZone(sServerFacade.GetAreaId(bot));
}

bool AttackEnemyFlagCarrierAction::isUseful()
{
    Unit* target = context->GetValue<Unit*>("enemy flag carrier")->Get();
    // Was bot->HasAura(...) (2026-07-27, fixed) - that's the "am I personally
    // carrying a flag" check used correctly one function above in
    // DpsAssistAction::isUseful() ("if carry flag, do not start fight"), but
    // it was copy-pasted here unchanged. Requiring the ATTACKING bot to
    // already be a flag carrier made this action nearly always false for a
    // normal bot chasing the enemy flag carrier, since regular chasers don't
    // carry a flag - confirmed live: bots chased flag carriers relentlessly
    // but never actually attacked once they caught up. The check belongs on
    // the TARGET (confirming it's genuinely still an active flag carrier),
    // not on the attacker.
    return target && sServerFacade.IsDistanceLessOrEqualThan(sServerFacade.getDistance2d(bot, target), 75.0f) && (target->HasAura(23333) || target->HasAura(23335));
}

bool SelectNewTargetAction::Execute(Event& event)
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (target && sServerFacade.UnitIsDead(target))
    {
        // Save the dead target for later looting
        ObjectGuid guid = target->getObjectGuid();
        if (guid)
        {
            AI_VALUE(LootObjectStack*, "available loot")->Add(guid);
        }
    }

    // Clear the target variables
    ObjectGuid attackTarget = AI_VALUE(ObjectGuid, "attack target");
    std::list<ObjectGuid> possible = AI_VALUE(std::list<ObjectGuid>, "possible targets no los");
    if (attackTarget && find(possible.begin(), possible.end(), attackTarget) == possible.end())
    {
        SET_AI_VALUE(ObjectGuid, "attack target", ObjectGuid());
    }

    ObjectGuid explicitAttackTarget = AI_VALUE(ObjectGuid, "explicit attack target");
    Unit* explicitTarget = explicitAttackTarget ? ai->GetUnit(explicitAttackTarget) : nullptr;
    if (explicitAttackTarget && (!explicitTarget ||
        !PossibleAttackTargetsValue::IsValid(explicitTarget, bot,
            sPlayerbotAIConfig.sightDistance, false, false)))
    {
        SET_AI_VALUE(ObjectGuid, "explicit attack target", ObjectGuid());
    }

    // Save the old target and clear the current target
    if(target)
    {
        SET_AI_VALUE(Unit*, "old target", target);
        SET_AI_VALUE(Unit*, "current target", nullptr);
    }

    // Stop attacking
    bot->SetSelectionGuid(ObjectGuid());
    ai->InterruptSpell();
    bot->AttackStop();


    bool moreAttackers = false;
    // Check if there is any enemy targets available to attack
    if (AI_VALUE(bool, "has attackers"))
    {
        if (ai->HasStrategy("pvp", BotState::BOT_STATE_COMBAT) ||
            ai->HasStrategy("duel", BotState::BOT_STATE_COMBAT))
        {
            // Check if there is an enemy player nearby
            if (AI_VALUE(bool, "has enemy player targets"))
            {
                moreAttackers = true;
                return ai->DoSpecificAction("attack enemy player", event, true);
            }
        }

        // Let the dps/tank assist pick a target to attack
        if (ai->HasStrategy("dps assist", BotState::BOT_STATE_NON_COMBAT))
        {
            moreAttackers = true;
            return ai->DoSpecificAction("dps assist", event, true);
        }
        else if (ai->HasStrategy("tank assist", BotState::BOT_STATE_NON_COMBAT))
        {
            moreAttackers = true;
            return ai->DoSpecificAction("tank assist", event, true);
        }
    }

    if (!moreAttackers)
    {
        // Stop pet attacking
        Pet* pet = bot->GetPet();
        if (pet)
        {
            UnitAI* creatureAI = ((Creature*)pet)->AI();
            if (creatureAI)
            {
                // Send pet action packet
                const ObjectGuid& petGuid = pet->getObjectGuid();
                const ObjectGuid& targetGuid = ObjectGuid();
                const uint8 flag = ACT_COMMAND;
                const uint32 spellId = COMMAND_FOLLOW;
                const uint32 command = (flag << 24) | spellId;

                WorldPacket data(CMSG_PET_ACTION);
                data << petGuid;
                data << command;
                data << targetGuid;
                bot->GetSession()->HandlePetAction(data);
            }
        }
    }

    return false;
}
