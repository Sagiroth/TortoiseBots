
#include "playerbot/playerbot.h"

#include "ReactionEngine.h"
#include <iomanip>

using namespace ai;

void Reaction::SetAction(Action* inAction)
{
    if (inAction)
    {
        SetDuration(inAction->GetDuration());
        action = inAction;
    }
}

bool Reaction::Update(uint32 elapsed)
{
    // Update remaining duration
    duration = duration > elapsed ? duration - elapsed : 0;

    // TO DO: Check if the reaction got interrupted (stun, knockback, ...)
    // ...

    // Return true when the reaction has finished
    return !IsActive();
}

ReactionEngine::ReactionEngine(PlayerbotAI* ai, AiObjectContext* factory, BotState engineState)
: Engine(ai, factory, engineState)
, aiReactionUpdateDelay(0U)
{

}

bool ReactionEngine::FindReaction(bool isStunned)
{
    // Steady-state fast path: with no live master, no transition, full
    // health/mana, no aura, no victim and no follow motion, the only
    // triggers that can still fire are the chat/packet commands (which need
    // a queued ExternalEvent, drained below) and the cell-grid hazard scan.
    // Skip the fan-out there; the delay bookkeeping in Update still runs.
    if (!HasReactionWork())
        return false;
    // Don't find a new reaction if the previous reaction is still running
    if(!IsReacting())
    {
        aiObjectContext->Update();
        ai->HandleCommands();

        // This will populate the queue to be processed with the reactions that can be triggered
        ProcessTriggers(false);

        ActionBasket* reactionItem = NULL;

        // Look for the best reaction (if any available)
        int iterations = 0;
        int iterationsPerTick = queue.Size() *  sPlayerbotAIConfig.iterationsPerTick;
        do
        {
            // Get the best reaction in the queue (sorted by relevance)
            reactionItem = queue.Peek();
            if (reactionItem)
            {
                const bool skipReactionPrerequisites = reactionItem->isSkipPrerequisites();
                float reactionRelevance = reactionItem->getRelevance();
                const Event& reactionEvent = reactionItem->getEvent();

                // Extract the reaction from the queue (removed)
                ActionNode* reactionNode = queue.Pop(reactionItem);
                if (reactionNode)
                {
                    Action* reaction = InitializeAction(reactionNode);
                    if (reaction)
                    {
                        // Update the reaction relevance
                        reaction->setRelevance(reactionRelevance);

                        // Check if the reaction is useful
                        if (reaction->isUseful() && (!isStunned || reaction->isUsefulWhenStunned()))
                        {
                            // Process the multipliers
                            for (std::list<Multiplier*>::iterator i = multipliers.begin(); i != multipliers.end(); i++)
                            {
                                reactionRelevance *= (*i)->GetValue(reaction);
                                reaction->setRelevance(reactionRelevance);
                                if (reactionRelevance <= 0.0f)
                                {
                                    // Multiplier made reaction useless
                                    break;
                                }
                            }

                            // Process prerequisites
                            if (!skipReactionPrerequisites)
                            {
                                // Add the prerequisites to the queue with a slight higher relevance than this action to be processed in the next iteration
                                if (MultiplyAndPush(reactionNode->getPrerequisites(), reactionRelevance + 0.02, false, reactionEvent, "prereq"))
                                {
                                    // Add this reaction to the queue again to be processed after the prerequisite
                                    PushAgain(reactionNode, reactionRelevance + 0.01, reactionEvent);
                                    continue;
                                }
                            }

                            // Check if the reaction is possible
                            if ((reactionRelevance > 0.0f) && reaction->isPossible())
                            {
                                // Reaction found
                                incomingReaction.SetAction(reaction);
                                incomingReaction.SetEvent(reactionEvent);
                                delete reactionNode;
                                break;
                            }
                            else
                            {
                                // Add the alternative reactions to the queue
                                MultiplyAndPush(reactionNode->getAlternatives(), reactionRelevance + 0.03, false, reactionEvent, "alt");
                            }
                        }
                    }

                    // Delete the reaction node
                    delete reactionNode;
                }
            }
        }
        while (reactionItem && ++iterations <= iterationsPerTick);

        // Remove the expired reactions
        queue.RemoveExpired();

        return incomingReaction.IsValid();
    }

    return false;
}

bool ReactionEngine::StartReaction()
{
    bool reactionExecuted = false;
    if (incomingReaction.IsValid())
    {
        // Execute the incoming reaction
        reactionExecuted = ListenAndExecute(incomingReaction.GetAction(), incomingReaction.GetEvent());
        botdiag::CountAction(ai->GetBot() ? ai->GetBot()->GetClass() : 0,
            incomingReaction.GetAction()->getName().c_str(), reactionExecuted);
        if (reactionExecuted)
        {
            // Move the incoming reaction to the ongoing reaction
            ongoingReaction = incomingReaction;
        }

        // Remove the incoming reaction
        incomingReaction.Reset();
    }

    return reactionExecuted;
}

void ReactionEngine::StopReaction()
{
    ongoingReaction.Reset();
    aiReactionUpdateDelay = 0U;

    // TO DO: Interrupt if the action is still running
    // ...
}

bool ReactionEngine::Update(uint32 elapsed, bool minimal, bool isStunned, bool& reactionFound)
{
    aiReactionUpdateDelay = aiReactionUpdateDelay > elapsed ? aiReactionUpdateDelay - elapsed : 0U;

    reactionFound = false;
    bool reactionFinished = false;

    // Can update reaction?
    if (CanUpdateAIReaction())
    {
        if (IsReacting())
        {
            if (ongoingReaction.Update(elapsed))
            {
                StopReaction();
                reactionFinished = true;
            }
        }
        else
            reactionFinished = true;

        if(reactionFinished)
        {
            if (HasIncomingReaction())
            {
                // Start the incoming reaction
                StartReaction();
            }
            else
            {
                // Look for an available reaction
                if (FindReaction(isStunned))
                {
                    reactionFound = true;
                }
            }
        }

        // Only add a reaction update delay if no reaction is pending or currently running
        if (!HasIncomingReaction() && !IsReacting())
        {
            if (aiReactionUpdateDelay < sPlayerbotAIConfig.reactDelay)
                aiReactionUpdateDelay = minimal ? sPlayerbotAIConfig.reactDelay * 10 : sPlayerbotAIConfig.reactDelay;
        }
    }

    // Return true if a reaction is pending or currently running
    return HasIncomingReaction() || IsReacting();
}

bool ReactionEngine::ListenAndExecute(Action* action, Event& event)
{
    bool actionExecuted = false;
    if (actionExecutionListeners.Before(action, event))
    {
        actionExecuted = actionExecutionListeners.AllowExecution(action, event) ? action->Execute(event) : true;
        if (actionExecuted)
        {
            if (!incomingReaction.GetAction()) //Prevent reset during action.
                incomingReaction.SetAction(action);
            ai->SetActionDuration(action);
        }
    }

    if (ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "do: ";
        out << action->getName();
        if (actionExecuted)
            out << " 1 (";
        else
            out << " 0 (";

        out << std::fixed << std::setprecision(2);
        out << action->getRelevance() << ")";

        if (!event.getSource().empty())
            out << " [" << event.getSource() << "]";

        out << " [reaction]";

        if(actionExecuted)
            out << " (duration: " << ((float)incomingReaction.GetDuration() / static_cast<float>(IN_MILLISECONDS)) << "s)";

        ai->TellPlayerNoFacing(ai->GetMaster(), out);
    }

    actionExecuted = actionExecutionListeners.OverrideResult(action, actionExecuted, event);
    actionExecutionListeners.After(action, actionExecuted, event);
    return actionExecuted;
}

ai::Action* ReactionEngine::InitializeAction(ActionNode* actionNode)
{
    Action* action = actionNode->getAction();
    if (!action)
    {
        action = aiObjectContext->GetAction(actionNode->getName());
        actionNode->setAction(action);
    }

    if (action)
    {
        action->SetReaction(true);
    }

    return action;
}

void ReactionEngine::SetReactionDuration(const Action* action)
{
    if (action && (IsReacting() || HasIncomingReaction()))
    {
        if (ongoingReaction.GetAction() == action)
        {
            ongoingReaction.SetDuration(action->GetDuration());
        }
        else if (incomingReaction.GetAction() == action)
        {
            incomingReaction.SetDuration(action->GetDuration());
        }
    }
}

void ReactionEngine::Reset()
{
    ongoingReaction.Reset();
    incomingReaction.Reset();
    aiReactionUpdateDelay = 0U;
}

bool ReactionEngine::CanUpdateAIReaction() const
{
    Player* bot = ai->GetBot();
    return (aiReactionUpdateDelay < 100U) &&
            bot->IsInWorld() &&
           !bot->IsBeingTeleported();
}
bool ReactionEngine::HasReactionWork() const
{
    // External triggers arrive only via queued chat commands or packet
    // handlers (HandleCommands drains the queue, HandlePacket sets
    // WorldPacketTrigger); with no queue and no live master to whisper,
    // every ChatCommandTrigger visit ends empty. State flips need a live
    // transition (combat/death/alive mismatch), potions need low health or
    // mana, aoe/dragon/spread need a target or group member in range, and
    // the bomb/mark/poison aura triggers need a matching danger aura
    // (HasReactionAuraWork). Check only cheap scalar state here; anything
    // ambiguous returns true and runs the full fan-out as before.
    Player* bot = ai->GetBot();
    if (!bot)
        return true;
    // Queued owner commands or combat/death/alive transitions always run.
    if (ai->HasRealPlayerMaster())
        return true;
    if (ai->IsStateActive(BotState::BOT_STATE_COMBAT) != bot->IsInCombat())
        return true;
    // Group attackers arrive before the core combat flag flips: a member
    // under attack must still run "combat start" this tick.
    if (!bot->GetAttackers().empty())
        return true;
    if (!ai->IsStateActive(BotState::BOT_STATE_DEAD) != bot->IsAlive())
        return true;
    // "stop follow" fires while the bot runs follow motion (pool bots follow
    // bot leaders); its action then self-vetoes when not following.
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
        return true;
    // "heal target full health" interrupts a preparing single-target heal.
    if (bot->IsNonMeleeSpellCasted(true))
        return true;
    // Damaged or low-resource bots may need potions, bandages, dispels.
    if (bot->GetHealth() < bot->GetMaxHealth())
        return true;
    // Dragon/spread triggers need a live victim or current target.
    if (bot->GetVictim())
        return true;
    if (bot->GetPowerType() == POWER_MANA && bot->GetPower(POWER_MANA) < bot->GetMaxPower(POWER_MANA))
        return true;
    // Aura-gated reaction triggers only test the bomb/mark/poison
    // families (raid bomb spell ids, 4H mark spell ids, dispellable poison
    // on self for "has poison debuff"): a holder map holding only
    // buffs/talents/racials can never fire them. Check those categories
    // directly (map probes, no aura walk) instead of map emptiness. Party
    // cure runs on the combat/non-combat engines, never here.
    if (HasReactionAuraWork(bot))
        return true;
    return false;
}

// True when the bot carries an aura any reaction trigger actually tests:
// the raid bomb spell ids, the 4H mark spell ids, or a dispellable poison
// on self ("has poison debuff" -> anti-venom). Passive talents, racials
// and buffs fall through and let FindReaction skip the fan-out.
bool ReactionEngine::HasReactionAuraWork(Player* bot) const
{
    static const uint32 bombSpells[] = { 20475, 23620, 18173, 23478, 28169 };
    for (uint32 spellId : bombSpells)
    {
        if (bot->HasAura(spellId))
            return true;
    }
    static const uint32 markSpells[] = { 28832, 28833, 28834, 28835 };
    for (uint32 spellId : markSpells)
    {
        if (bot->HasAura(spellId))
            return true;
    }
    if (ai->HasAuraToDispel(bot, DISPEL_POISON))
        return true;
    return false;
}

const Reaction* ReactionEngine::GetReaction() const
{
    const Reaction* reaction = nullptr;
    if (ongoingReaction.IsValid())
    {
        reaction = &ongoingReaction;
    }
    else if (incomingReaction.IsValid())
    {
        reaction = &incomingReaction;
    }

    return reaction;
}