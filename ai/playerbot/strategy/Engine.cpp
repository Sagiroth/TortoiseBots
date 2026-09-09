
#include "playerbot/playerbot.h"
#include <stdarg.h>
#include <iomanip>

#include "Engine.h"
#include "playerbot/PlayerbotAIConfig.h"
// #include "playerbot/PerformanceMonitor.h" // E2E green
#include "playerbot/BotActionLog.h"

#ifdef BUILD_ELUNA
#include "LuaEngine/LuaEngine.h"
#endif

using namespace ai;
// M1 decision trail: short engine-state label so TICK lines identify which of
// the four engines (combat/non-combat/dead/reaction) emitted a decision.
static const char* BotStateName(BotState state)
{
    switch (state)
    {
        case BotState::BOT_STATE_COMBAT: return "combat";
        case BotState::BOT_STATE_NON_COMBAT: return "non-combat";
        case BotState::BOT_STATE_DEAD: return "dead";
        case BotState::BOT_STATE_REACTION: return "reaction";
        default: return "all";
    }
}

Engine::Engine(PlayerbotAI* ai, AiObjectContext *factory, BotState state) : PlayerbotAIAware(ai), aiObjectContext(factory), state(state)
{
    lastRelevance = 0.0f;
    lastExecutedAction = nullptr;
}

bool ActionExecutionListeners::Before(Action* action, const Event& event)
{
    bool result = true;
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i!=listeners.end(); i++)
    {
        result &= (*i)->Before(action, event);
    }
    return result;
}

void ActionExecutionListeners::After(Action* action, bool executed, const Event& event)
{
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i!=listeners.end(); i++)
    {
        (*i)->After(action, executed, event);
    }
}

bool ActionExecutionListeners::OverrideResult(Action* action, bool executed, const Event& event)
{
    bool result = executed;
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i!=listeners.end(); i++)
    {
        result = (*i)->OverrideResult(action, result, event);
    }
    return result;
}

bool ActionExecutionListeners::AllowExecution(Action* action, const Event& event)
{
    bool result = true;
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i!=listeners.end(); i++)
    {
        result &= (*i)->AllowExecution(action, event);
    }
    return result;
}

ActionExecutionListeners::~ActionExecutionListeners()
{
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i!=listeners.end(); i++)
    {
        delete *i;
    }
    listeners.clear();
}


Engine::~Engine(void)
{
    Reset();

    strategies.clear();
}

bool Engine::Reset()
{
    if (inDoNextAction)
    {
        reinitPending = true;
        LogAction("S:reinit deferred");
        return false;
    }

    ActionNode* action = NULL;
    do
    {
        action = queue.Pop();
        if (!action) break;
        delete action;
    } while (true);

    for (std::list<TriggerNode*>::iterator i = triggers.begin(); i != triggers.end(); i++)
    {
        TriggerNode* trigger = *i;
        delete trigger;
    }
    triggers.clear();

    for (std::list<Multiplier*>::iterator i = multipliers.begin(); i != multipliers.end(); i++)
    {
        Multiplier* multiplier = *i;
        delete multiplier;
    }
    multipliers.clear();

    return true;
}
std::string Engine::FailureKey(Action* action, Event& event, ActionResult result) const
{
    uint64_t targetGuid = 0;
    if (Unit* target = action->GetTarget())
        targetGuid = target->GetObjectGuid().GetRawValue();
    // RPG actions expose self as target; distinguish their destination too.
    uint64_t destGuid = aiObjectContext->GetValue<GuidPosition>("rpg target")->Get().GetRawValue();
    return ActionFailureBackoff::Key(action->getName(), event.getSource(), targetGuid, destGuid,
        static_cast<uint32_t>(result));
}

bool Engine::AllowBackgroundRetry(Action* action, Event& event) const
{
    Player* const bot = ai->GetBot();
    return sPlayerbotAIConfig.failedActionRetryBaseMs && sPlayerbotAIConfig.failedActionRetryMaxMs &&
        state == BotState::BOT_STATE_NON_COMBAT && bot && bot->IsAlive() && !bot->IsInCombat() &&
        !ai->HasRealPlayerMaster() && !ai->IsRealPlayer() && !ai->IsOwnedBot() &&
        !action->IsReaction() && !event.getOwner() && event.getPacket().empty() &&
        action->getRelevance() < ACTION_HIGH;
}

bool Engine::IsFailureBackedOff(Action* action, Event& event) const
{
    if (!AllowBackgroundRetry(action, event))
        return false;
    return actionFailures.IsBackedOff(FailureKey(action, event, ACTION_RESULT_FAILED),
        WorldTimer::getMSTime()) ||
        actionFailures.IsBackedOff(FailureKey(action, event, ACTION_RESULT_IMPOSSIBLE),
        WorldTimer::getMSTime());
}

void Engine::RecordFailure(Action* action, Event& event, ActionResult result)
{
    if (!AllowBackgroundRetry(action, event))
        return;
    actionFailures.Record(FailureKey(action, event, result), WorldTimer::getMSTime(),
        sPlayerbotAIConfig.failedActionRetryBaseMs, sPlayerbotAIConfig.failedActionRetryMaxMs,
        sPlayerbotAIConfig.failedActionCacheMaxEntries, sPlayerbotAIConfig.failedActionCacheTtlMs);
}

void Engine::ClearActionFailures(Action* action, Event& event)
{
    actionFailures.Clear(FailureKey(action, event, ACTION_RESULT_FAILED),
        FailureKey(action, event, ACTION_RESULT_IMPOSSIBLE));
}

void Engine::RefreshFailureContext()
{
    Player* const bot = ai->GetBot();
    if (!bot)
        return;
    // Any physical or resource change can unstick a failure: moved, looted,
    // healed, regained mana. Map changes clear through the transition path.
    if (failX != bot->GetPositionX() || failY != bot->GetPositionY() || failZ != bot->GetPositionZ() ||
        failMoney != bot->GetMoney() || failHealth != bot->GetHealth() ||
        failMana != bot->GetPower(POWER_MANA))
        actionFailures.ClearAll();
    failX = bot->GetPositionX(); failY = bot->GetPositionY(); failZ = bot->GetPositionZ();
    failMoney = bot->GetMoney(); failHealth = bot->GetHealth(); failMana = bot->GetPower(POWER_MANA);
    actionFailures.Prune(WorldTimer::getMSTime(), sPlayerbotAIConfig.failedActionCacheTtlMs);
}

void Engine::DrainQueue()
{
    ActionNode* node = NULL;
    do
    {
        node = queue.Pop();
        if (!node) break;
        delete node;
    } while (true);
}

void Engine::Init()
{
    if (!Reset())
        return;

    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        Strategy* strategy = i->second;
        strategy->InitMultipliers(multipliers, state);
        strategy->InitTriggers(triggers, state);
        std::vector<Multiplier*> modernMultipliers;
        strategy->InitMultipliers(modernMultipliers);
        for (Multiplier* multiplier : modernMultipliers)
            multipliers.push_back(multiplier);

        std::vector<TriggerNode*> modernTriggers;
        strategy->InitTriggers(modernTriggers);
        for (TriggerNode* trigger : modernTriggers)
            triggers.push_back(trigger);

        MultiplyAndPush(strategy->getDefaultActions(state), 0.0f, false, Event(), "default");
        MultiplyAndPush(strategy->getDefaultActions(), 0.0f, false, Event(), "default");
    }

    // M2 anchor: one line per graph rebuild so topology changes (and only
    // topology changes) are visible in the trail. Proves the no-op
    // property: a ChangeStrategy that leaves the signature unchanged runs
    // no rebuild and emits no anchor.
    LogAction("S:init done state=%s strats=%s triggers=%u multipliers=%u",
        BotStateName(state), StrategySignature().c_str(),
        (unsigned)triggers.size(), (unsigned)multipliers.size());

}

bool Engine::DoNextAction(Unit* unit, int depth, bool minimal, bool isStunned)
{
    LogAction("--- AI Tick --- state=%s strats=%s", BotStateName(state), StrategySignature().c_str());
    if (sPlayerbotAIConfig.logValuesPerTick)
        LogValues();

    bool actionExecuted = false;
    ActionBasket* basket = NULL;

    // Issue #84 (P2): never run queued work while phased out of the world.
    // The ack path bumps the AI's transition generation (ticks are skipped
    // while teleporting), so arrival drains even for short same-map hops the
    // position detector cannot see. Map id + 3D continuity backstop transfers
    // the ack path never sees. Drain is local: no Reset()/Init() interplay,
    // strategies and triggers are untouched. Walking across zone lines
    // moves continuously and never drains.
    Player* const tickBot = ai->GetBot();
    if (!tickBot)
        return false;
    uint32_t const tickMapId = tickBot->GetMapId();
    TransitionTracker::Event const transition = transitions.Update(tickBot->IsInWorld(),
        tickBot->IsBeingTeleported(), tickMapId,
        tickBot->GetPositionX(), tickBot->GetPositionY(), tickBot->GetPositionZ(),
        ai->GetTransitionGeneration());
    if (transition == TransitionTracker::AWAY)
        return false;
    if (transition != TransitionTracker::NONE)
    {
        LogAction("transition %d on map %u: draining %s queue", (int)transition, tickMapId, BotStateName(state));
        DrainQueue();
        actionFailures.ClearAll();
    }
    RefreshFailureContext();

    bool const wasInDoNextAction = inDoNextAction;
    inDoNextAction = true;

    time_t currentTime = time(0);
    aiObjectContext->Update();
    ProcessTriggers(minimal);
    PushDefaultActions();

    std::vector<Action*> modifiedActions;

    int iterations = 0;
    int iterationsPerTick = queue.Size() * (minimal ? (uint32)(sPlayerbotAIConfig.iterationsPerTick / 2) : sPlayerbotAIConfig.iterationsPerTick);
    do
    {
        // Issue #84: an action can teleport the bot (hearth, taxi, summon).
        // Stop the walk at that ownership boundary and mark the tracker away
        // so arrival drains even if the generation bump was somehow missed.
        // No reinit: queue stays for arrival.
        if (!tickBot->IsInWorld() || tickBot->IsBeingTeleported() || tickBot->GetMapId() != tickMapId)
        {
            transitions.NoteAway();
            LogAction("transition mid-walk: stopping %s queue", BotStateName(state));
            break;
        }
        basket = queue.Peek();
        if (basket)
        {
            float relevance = basket->getRelevance(), oldRelevance = relevance; // just for reference
            bool skipPrerequisites = basket->isSkipPrerequisites();
            Event event = basket->getEvent();
            if (minimal && (relevance < 100))
            {
                // Queue::Peek returns the highest relevance basket, so every
                // remaining basket is also below the minimal cutoff. Defer the
                // queue intact rather than repeatedly peeking the same entry
                // until the iteration budget is exhausted.
                LogAction("minimal tick defers low-relevance queue");
                break;
            }
            // NOTE: queue.Pop() deletes basket
            ActionNode* actionNode = queue.Pop();
            Action* action = InitializeAction(actionNode);

            std::string actionName = (action ? action->getName() : "unknown");
            if (!event.getSource().empty())
                actionName += " <" + event.getSource() + ">";

            // E2E green: PerformanceMonitor stub

            if(action)
                action->setRelevance(relevance);

            if (!action)
            {
                if (sPlayerbotAIConfig.CanLogAction(ai, actionNode->getName(), false, ""))
                {
                    std::ostringstream out;
                    out << "try: ";
                    out << actionNode->getName();
                    out << " unknown (";

                    out << std::fixed << std::setprecision(3);
                    out << relevance << ")";

                    if (!event.getSource().empty())
                        out << " [" << event.getSource() << "]";

                    if (ai->GetMaster())
                    {
                        ai->TellPlayerNoFacing(ai->GetMaster(), out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
                    }
                    else
                    {
                        ai->GetBot()->Say(out.str(), (ai->GetBot()->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
                    }
                }
                LogAction("A:%s - UNKNOWN src=%s base=%.3f", actionNode->getName().c_str(), event.getSource().c_str(), relevance);
            }
            else
            {
                bool isUseful = false;
                if (!isStunned || action->isUsefulWhenStunned())
                {
                    // E2E green: PerformanceMonitor stub
                    isUseful = action->isUseful();
// E2E green: pmo stub
                }

                if (isUseful)
                {
                    if (std::find(modifiedActions.begin(), modifiedActions.end(), action) == modifiedActions.end())
                    {
                        for (std::list<Multiplier*>::iterator i = multipliers.begin(); i != multipliers.end(); i++)
                        {
                            Multiplier* multiplier = *i;
                            // M1 decision trail: record each multiplier that changes a
                            // candidate's relevance (factor != 1), not just the one
                            // that zeroes it. Silent factors are the usual reason a
                            // sensible rotation looks arbitrary in hindsight.
                            float beforeMult = relevance;
                            float factor = multiplier->GetValue(action);
                            relevance *= factor;

                            action->setRelevance(relevance);
                            if (factor != 1.0f)
                                LogAction("A:%s - MULT %s x%.3f (%.3f->%.3f)", action->getName().c_str(), multiplier->getName().c_str(), factor, beforeMult, relevance);
                            if (!relevance)
                            {
                                LogAction("Multiplier %s made action %s useless", multiplier->getName().c_str(), action->getName().c_str());
                                break;
                            }
                        }
                    }
                    // Issue #84 (P1): the backoff gate runs before
                    // prerequisites and the possibility check. A backing-off
                    // background action is dropped here (not executed, no
                    // alternatives, no prereq work). Its trigger re-fires
                    // while the cause persists, so execution resumes on
                    // expiry without queue churn.
                    if (IsFailureBackedOff(action, event))
                    {
                        LogAction("A:%s - BACKOFF src=%s base=%.3f eff=%.3f", action->getName().c_str(), event.getSource().c_str(), oldRelevance, relevance);
                        delete actionNode;
                        continue;
                    }

                    ActionBasket* peekAction = queue.Peek();
                    if (relevance < oldRelevance && peekAction && peekAction->getRelevance() > relevance) //Relevance changed. Try again.
                    {
                        modifiedActions.push_back(action);
                        PushAgain(actionNode, relevance, event);
                        continue;
                    }

                    if (!skipPrerequisites)
                    {
                        LogAction("A:%s - PREREQ src=%s base=%.3f eff=%.3f", action->getName().c_str(), event.getSource().c_str(), oldRelevance, relevance);
                        if (MultiplyAndPush(actionNode->getPrerequisites(), relevance + 0.02, false, event, "prereq"))
                        {
                            PushAgain(actionNode, relevance + 0.01, event);
                            continue;
                        }
                    }

                    // E2E green: PerformanceMonitor stub
                    bool isPossible = action->isPossible();
// E2E green: pmo stub

                    if (isPossible && relevance)
                    {
                        // E2E green: PerformanceMonitor stub
                        actionExecuted = ListenAndExecute(action, event);
// E2E green: pmo stub

#ifdef PLAYERBOT_ELUNA
                        // used by eluna
                        if (Eluna* e = ai->GetBot()->GetEluna())
                            e->OnActionExecute(ai, action->getName(), actionExecuted);
#endif

                        if (actionExecuted)
                        {
                            LogAction("A:%s - OK src=%s base=%.3f eff=%.3f", action->getName().c_str(), event.getSource().c_str(), oldRelevance, relevance);
                            ClearActionFailures(action, event);
                            MultiplyAndPush(actionNode->getContinuers(), 0, false, event, "cont");
                            lastRelevance = relevance;
                            delete actionNode;
                            break;
                        }
                        else
                        {
                            LogAction("A:%s - FAILED src=%s base=%.3f eff=%.3f", action->getName().c_str(), event.getSource().c_str(), oldRelevance, relevance);
                            RecordFailure(action, event, ACTION_RESULT_FAILED);
                            MultiplyAndPush(actionNode->getAlternatives(), relevance + 0.03, false, event, "alt");
                        }
                    }
                    else
                    {
                        if (sPlayerbotAIConfig.CanLogAction(ai,actionNode->getName(), false, ""))
                        {
                            std::ostringstream out;
                            out << "try: ";
                            out << action->getName();
                            out << " impossible (";

                            out << std::fixed << std::setprecision(3);
                            out << action->getRelevance() << ")";

                            if (!event.getSource().empty())
                                out << " [" << event.getSource() << "]";

        if (ai->GetMaster())
                            {
                                ai->TellPlayerNoFacing(ai->GetMaster(), out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
                            }
                            else
                            {
                                ai->GetBot()->Say(out.str(), (ai->GetBot()->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
                            }
                        }
                        LogAction("A:%s - IMPOSSIBLE src=%s base=%.3f eff=%.3f", action->getName().c_str(), event.getSource().c_str(), oldRelevance, relevance);
                        RecordFailure(action, event, ACTION_RESULT_IMPOSSIBLE);
                        MultiplyAndPush(actionNode->getAlternatives(), relevance + 0.03, false, event, "alt");
                    }
                }
                else
                {
                    if (sPlayerbotAIConfig.CanLogAction(ai,actionNode->getName(), false, ""))
                    {
                        std::ostringstream out;
                        out << "try: ";
                        out << action->getName();
                        out << " useless (";

                        out << std::fixed << std::setprecision(3);
                        out << action->getRelevance() << ")";

                        if (!event.getSource().empty())
                            out << " [" << event.getSource() << "]";

        if (ai->GetMaster())
                        {
                            ai->TellPlayerNoFacing(ai->GetMaster(), out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
                        }
                        else
                        {
                            ai->GetBot()->Say(out.str(), (ai->GetBot()->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
                        }
                    }
                    lastRelevance = relevance;
                    LogAction("A:%s - USELESS src=%s base=%.3f eff=%.3f", action->getName().c_str(), event.getSource().c_str(), oldRelevance, relevance);
                }
            }
            delete actionNode;
        }
    }
    while (basket && ++iterations <= iterationsPerTick);

    /*
    if (!basket)
    {
        lastRelevance = 0.0f;
        PushDefaultActions();
        if (queue.Peek() && depth < 2)
            return DoNextAction(unit, depth + 1, minimal, isStunned);
    }
    */

    // MEMORY FIX TEST
 /*   do {
        basket = queue.Peek();
        if (basket) {
            // NOTE: queue.Pop() deletes basket
            delete queue.Pop();
        }
    } while (basket);*/

    if (time(0) - currentTime > 1) {
        LogAction("too long execution");
    }

    if (!actionExecuted)
        LogAction("no actions executed");

    queue.RemoveExpired();

    inDoNextAction = wasInDoNextAction;
    if (!inDoNextAction && reinitPending)
    {
        reinitPending = false;
        Init();
    }

    return actionExecuted;
}

ActionNode* Engine::CreateActionNode(const std::string& name)
{
    ActionNode* actionNode = nullptr;
    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        Strategy* strategy = i->second;
        actionNode = strategy->GetAction(name);
        if (actionNode)
        {
            break;
        }
    }

    if (!actionNode)
    {
        actionNode = new ActionNode(name);
    }

    return actionNode;
}

bool Engine::MultiplyAndPush(NextAction** actions, float forceRelevance, bool skipPrerequisites, const Event& event, const char* pushType)
{
    bool pushed = false;
    if (actions)
    {
        for (int j=0; actions[j]; j++)
        {
            NextAction* nextAction = actions[j];
            if (nextAction)
            {
                ActionNode* actionNode = CreateActionNode(nextAction->getName());
                InitializeAction(actionNode);

                bool shouldPush = false;
                float k = nextAction->getRelevance();
                if (forceRelevance > 0.0f)
                {
                    k = forceRelevance;
                }
                else if (strcmp(pushType, "default") == 0)
                {
                    k -= 200.0f;
                    shouldPush = true;
                }
                else if (strcmp(pushType, "prereq") == 0 || strcmp(pushType, "alt") == 0 || strcmp(pushType, "again") == 0)
                {
                    k = forceRelevance;
                    shouldPush = true;
                }

                if (!shouldPush)
                {
                    shouldPush = k > 0.0f;
                }

                if (shouldPush)
                {
                    LogAction("PUSH:%s - %f (%s) src=%s", actionNode->getName().c_str(), k, pushType, event.getSource().c_str());
                    queue.Push(new ActionBasket(actionNode, k, skipPrerequisites, event));
                    pushed = true;
                }
                else
                {
                    delete actionNode;
                }

                delete nextAction;
            }
            else
                break;
        }
        delete[] actions;
    }
    return pushed;
}

bool Engine::MultiplyAndPush(const std::vector<NextAction>& actions, float forceRelevance,
                             bool skipPrerequisites, const Event& event, const char* pushType)
{
    NextAction** compatibleActions = new NextAction*[actions.size() + 1];
    size_t index = 0;
    for (const NextAction& action : actions)
        compatibleActions[index++] = new NextAction(action);

    compatibleActions[index] = NULL;
    return MultiplyAndPush(compatibleActions, forceRelevance, skipPrerequisites, event, pushType);
}

ActionResult Engine::ExecuteAction(const std::string& name, Event& event)
{
    ActionResult actionResult = ACTION_RESULT_UNKNOWN;
    ActionNode* actionNode = CreateActionNode(name);
    if (actionNode)
    {
        // E2E green: PerformanceMonitor stub
        Action* action = InitializeAction(actionNode);
        if (action)
        {
            // Issue #84: an explicit command acts without delay, even when
            // the same background action is backing off. No backoff gate on
            // this path by design.
            ClearActionFailures(action, event);
            // E2E green: PerformanceMonitor stub
            bool isUseful = action->isUseful();
// E2E green: pmo stub

            if (isUseful)
            {
                // E2E green: PerformanceMonitor stub
                bool isPossible = action->isPossible();
// E2E green: pmo stub

                if (isPossible)
                {
                    action->MakeVerbose(event.getOwner() != nullptr);
                    // E2E green: PerformanceMonitor stub
                    bool executionResult = ListenAndExecute(action, event);
// E2E green: pmo stub

                    MultiplyAndPush(action->getContinuers(), 0.0f, false, event, "default");
                    actionResult = executionResult ? ACTION_RESULT_OK : ACTION_RESULT_FAILED;
                }
                else
                {
                    actionResult = ACTION_RESULT_IMPOSSIBLE;
                }
            }
            else
            {
                actionResult = ACTION_RESULT_USELESS;
            }
        }
        delete actionNode;
    }

    return actionResult;
}

bool Engine::QueueAction(const std::string& name, float relevance, const Event& event)
{
    ActionNode* actionNode = CreateActionNode(name);
    if (!actionNode)
        return false;

    if (!InitializeAction(actionNode))
    {
        delete actionNode;
        return false;
    }

    queue.Push(new ActionBasket(actionNode, relevance, false, event));
    return true;
}

bool Engine::CanExecuteAction(const std::string& name, bool isUseful, bool isPossible)
{
    bool result = true;
    ActionNode* actionNode = CreateActionNode(name);
    if (actionNode)
    {
        Action* action = InitializeAction(actionNode);
        if (action)
        {
            if (isUseful)
            {
                result &= action->isUseful();
            }

            if (isPossible)
            {
                result &= action->isPossible();
            }
        }

        delete actionNode;
    }

    return result;
}

void Engine::addStrategy(const std::string& name)
{
    std::string const signatureBefore = StrategySignature();

    // The second argument means "rebuild now", and initMode means the opposite
    // - hold rebuilds back until the bulk change is done - so passing one as
    // the other had it exactly backwards. Neither removal needs its own
    // rebuild: they belong to this add, which rebuilds once at the end.
    removeStrategy(name, false);

    Strategy* strategy = aiObjectContext->GetStrategy(name);
    if (strategy)
    {
        std::set<std::string> siblings = aiObjectContext->GetSiblingStrategy(name);
        for (std::set<std::string>::iterator i = siblings.begin(); i != siblings.end(); i++)
        {
            removeStrategy(*i, false);
        }

        LogAction("S:+%s", strategy->getName().c_str());
        strategies[strategy->getName()] = strategy;
        strategy->OnStrategyAdded(state);
    }

    // Init() empties the action queue, so only pay it when the strategy set
    // actually moved. Re-adding a strategy the engine already carries used to
    // wipe the queue for nothing.
    if (!initMode && StrategySignature() != signatureBefore)
    {
        Init();
    }
}

void Engine::addStrategies(std::string first, ...)
{
	addStrategy(first);

	va_list vl;
	va_start(vl, first);

	const char* cur;
	do
	{
		cur = va_arg(vl, const char*);
		if (cur)
			addStrategy(cur);
	}
	while (cur);

	va_end(vl);
}

bool Engine::removeStrategy(const std::string& name, bool init)
{
    std::map<std::string, Strategy*>::iterator i = strategies.find(name);
    if (i == strategies.end())
        return false;

    LogAction("S:-%s", name.c_str());
    i->second->OnStrategyRemoved(state);
    strategies.erase(i);

    if (init)
    {
        Init();
    }

    return true;
}

void Engine::removeAllStrategies()
{
    strategies.clear();
    Init();
}

void Engine::toggleStrategy(const std::string& name)
{
    if (!removeStrategy(name, !initMode))
        addStrategy(name);
}

bool Engine::HasStrategy(const std::string& name)
{
    return strategies.find(name) != strategies.end();
}

Strategy* Engine::GetStrategy(const std::string& name) const
{
    auto i = strategies.find(name);
    if (i != strategies.end())
    {
        return i->second;
    }

    return nullptr;
}

void Engine::ProcessTriggers(bool minimal)
{
    for (std::list<TriggerNode*>::iterator i = triggers.begin(); i != triggers.end(); i++)
    {
        TriggerNode* node = *i;
        if (!node)
            continue;

        Trigger* trigger = node->getTrigger();
        if (!trigger)
        {
            trigger = aiObjectContext->GetTrigger(node->getName());
            node->setTrigger(trigger);
        }
        if (!trigger)
            continue;

        if (trigger->IsAlreadyTriggered() || trigger->needCheck())
        {
            if (minimal && node->getFirstRelevance() < 100)
                continue;
            // E2E green: PerformanceMonitor stub
            Event event = trigger->Check();

#ifdef PLAYERBOT_ELUNA
            // used by eluna
            if (Eluna* e = ai->GetBot()->GetEluna())
                e->OnTriggerCheck(ai, trigger->getName(), !event ? false : true);
#endif

            if (!event)
                continue;

            MultiplyAndPush(node->getHandlers(), 0.0f, false, event, "trigger");
            LogAction("T:%s src=%s", trigger->getName().c_str(), event.getSource().c_str());
        }
    }

    for (std::list<TriggerNode*>::iterator i = triggers.begin(); i != triggers.end(); i++)
    {
        Trigger* trigger = (*i)->getTrigger();
        if (trigger) trigger->Reset();
    }
}

void Engine::PushDefaultActions()
{
    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        Strategy* strategy = i->second;
        MultiplyAndPush(strategy->getDefaultActions(state), 0.0f, false, Event(), "default");
        MultiplyAndPush(strategy->getDefaultActions(), 0.0f, false, Event(), "default");
    }
}

std::string Engine::ListStrategies()
{
    std::string s;
    if (strategies.empty())
        return s;

    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        s.append(i->first);
        s.append(", ");
    }
    return s.substr(0, s.length() - 2);
}

std::list<std::string_view> Engine::GetStrategies()
{
    std::list<std::string_view> result;
    for (const auto& strategy : strategies)
    {
        result.push_back(strategy.first);
    }
    return result;
}

void Engine::PushAgain(ActionNode* actionNode, float relevance, const Event& event)
{
    NextAction** nextAction = new NextAction*[2];
    nextAction[0] = new NextAction(actionNode->getName(), relevance);
    nextAction[1] = NULL;
    MultiplyAndPush(nextAction, relevance, true, event, "again");
    delete actionNode;
}

bool Engine::ContainsStrategy(StrategyType type)
{
	for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
	{
		Strategy* strategy = i->second;
		if (strategy->GetType() & type)
			return true;
	}
	return false;
}

Action* Engine::InitializeAction(ActionNode* actionNode)
{
    Action* action = actionNode->getAction();
    if (!action)
    {
        action = aiObjectContext->GetAction(actionNode->getName());
        actionNode->setAction(action);
    }

    if (action)
    {
        action->SetReaction(false);
    }

    return action;
}

bool Engine::ListenAndExecute(Action* action, Event& event)
{
    bool actionExecuted = false;
    Action* prevExecutedAction = lastExecutedAction;
    if (actionExecutionListeners.Before(action, event))
    {
        ai->SetLastEvent(event);
        sLog.outString("TortoiseBots AI: Engine executing Action=%s Trigger=%s bot=%s",
        action->getName().c_str(), event.getSource().c_str(), ai->GetBot()->GetName());
    actionExecuted = actionExecutionListeners.AllowExecution(action, event) ? action->Execute(event) : true;
        if (actionExecuted)
        {
            ai->SetActionDuration(action);
            lastExecutedAction = action;
        }
    }

    std::string lastActionName = prevExecutedAction ? prevExecutedAction->getName() : "";
    if (sPlayerbotAIConfig.CanLogAction(ai, action->getName(), true, lastActionName))
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

        if(!event.getSource().empty())
            out << " [" << event.getSource() << "]";

        if (actionExecuted)
        {
            const uint32 actionDuration = action->GetDuration();
            if (actionDuration > 0)
            {
                out << " (duration: " << ((float)actionDuration / static_cast<float>(IN_MILLISECONDS)) << "s)";
            }
        }

        if (ai->GetMaster())
        {
            ai->TellPlayerNoFacing(ai->GetMaster(), out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
        }
        else
        {
            ai->GetBot()->Say(out.str(), (ai->GetBot()->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
        }
    }

    if (ai->HasStrategy("debug threat", BotState::BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        AiObjectContext* context = ai->GetAiObjectContext();

        float deltaThreat = LOG_AI_VALUE(float, "my threat::current target")->GetDelta(5.0f);

        float currentThreat = AI_VALUE2(float, "my threat", "current target");
        float tankThreat = AI_VALUE2(float, "tank threat", "current target");
        float relThreat = AI_VALUE2(uint8, "threat", "current target");

        out << "threat: " << int32(currentThreat)<< "+" << int32(deltaThreat) << " / " << int32(tankThreat) << " ||| " << relThreat;

        ai->TellPlayerNoFacing(ai->GetMaster(), out);
    }

    actionExecuted = actionExecutionListeners.OverrideResult(action, actionExecuted, event);
    actionExecutionListeners.After(action, actionExecuted, event);
    return actionExecuted;
}

void Engine::LogAction(const char* format, ...)
{
    char buf[1024];

    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    lastAction += "|";
    lastAction += buf;
    if (lastAction.size() > 512)
    {
        lastAction = lastAction.substr(512);
        size_t pos = lastAction.find("|");
        lastAction = (pos == std::string::npos ? "" : lastAction.substr(pos));
    }

    Player* bot = ai->GetBot();
    if (sPlayerbotAIConfig.logInGroupOnly && !bot->GetGroup())
        return;

    sLog.outDetail( "%s %s", bot->GetName(), buf);

    // BotActionLog tee: every PUSH/A/Tick line also lands in the bot's
    // per-bot file under logs/bots/ when AiPlayerbot.EnableActionLog=1.
    // Tag heuristic extracts the first colon-prefix from `buf` so the
    // per-bot log gets useful tags (PUSH / A / T / etc.) instead of
    // a single "ACTION".
    const char* tag = "ENGINE";
    if (strncmp(buf, "PUSH:", 5) == 0)             tag = "PUSH";
    else if (strncmp(buf, "A:", 2) == 0)           tag = "ACTION";
    else if (strncmp(buf, "T:", 2) == 0)           tag = "TRIGGER_REASON";
    else if (strncmp(buf, "--- AI Tick", 11) == 0) tag = "TICK";
    else if (strncmp(buf, "no actions", 10) == 0)  tag = "NO_ACTION";
    ai::botdiag::BotActionLog::Write(ai, tag, "%s", buf);
}

void Engine::ChangeStrategy(const std::string& names)
{
    std::vector<std::string> splitted = split(names, ',');

    // Each entry would otherwise rebuild every strategy's triggers, although
    // only the set left at the end matters. Hold the rebuilds back for the
    // whole list and do one afterwards - the same thing
    // PlayerbotAI::ResetStrategies does around its bulk change.
    std::string const signatureBefore = StrategySignature();

    bool const wasInitMode = initMode;
    initMode = true;

    for (std::vector<std::string>::iterator i = splitted.begin(); i != splitted.end(); i++)
    {
        const char* name = i->c_str();
        switch (name[0])
        {
            case '+':
            {
                addStrategy(name+1);
                break;
            }
            case '-':
            {
                removeStrategy(name+1, false);
                break;
            }
            case '~':
            {
                toggleStrategy(name+1);
                break;
            }
        }
    }

    initMode = wasInitMode;

    // Caller is in a bulk change of its own - it will rebuild when it is done.
    //
    // The signature guard is what keeps a no-op change cheap. Init() calls
    // Reset(), which drains `queue` outright - so a ChangeStrategy issued from
    // inside an action's Execute() destroys every basket DoNextAction has not
    // popped yet, and the do-while at :326 exits on a null Peek(). BGTactics
    // fires exactly that: `ai->ChangeStrategy("-buff", BOT_STATE_NON_COMBAT)`
    // (BattleGroundTactics.cpp:2716-2718) on every tick of a battleground in
    // progress, whether or not "buff" is still attached. In WSG that ran on the
    // relevance-70 `bg check flag` action and killed the rest of the tick, so
    // `bg move to objective` at relevance 1.0 was queued 23,908 times and
    // popped none.
    if (!initMode && StrategySignature() != signatureBefore)
        Init();
}

std::string Engine::StrategySignature() const
{
    // strategies is an ordered map, so equal sets give equal strings.
    std::string signature;
    for (std::map<std::string, Strategy*>::const_iterator i = strategies.begin(); i != strategies.end(); ++i)
    {
        signature += i->first;
        signature += "|";
    }

    return signature;
}

void Engine::PrintStrategies(Player* requester, const std::string& engineType)
{
    std::string engineStrategies = engineType;
    engineStrategies.append(" Strategies: ");
    engineStrategies.append(ListStrategies());
    ai->TellPlayer(requester, engineStrategies, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, true);
}

void Engine::LogValues()
{
    Player* bot = ai->GetBot();
    if (sPlayerbotAIConfig.logInGroupOnly && !bot->GetGroup())
        return;

    std::string text = ai->GetAiObjectContext()->FormatValues();
    sLog.outDebug( "Values for %s: %s", bot->GetName(), text.c_str());
}
