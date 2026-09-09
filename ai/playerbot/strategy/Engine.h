#pragma once

#include "Action.h"
#include "ActionFailureBackoff.h"
#include "Queue.h"
#include "Trigger.h"
#include "Multiplier.h"
#include "AiObjectContext.h"
#include "Strategy.h"
#include "playerbot/BotState.h"

namespace ai
{
    class ActionExecutionListener
    {
    public:
        virtual bool Before(Action* action, const Event& event) = 0;
        virtual bool AllowExecution(Action* action, const Event& event) = 0;
        virtual void After(Action* action, bool executed, const Event& event) = 0;
        virtual bool OverrideResult(Action* action, bool executed, const Event& event) = 0;
        virtual ~ActionExecutionListener() {};
    };

    // -----------------------------------------------------------------------------------------------------------------------

    class ActionExecutionListeners : public ActionExecutionListener
    {
    public:
        virtual ~ActionExecutionListeners() override;

    // ActionExecutionListener
    public:
        virtual bool Before(Action* action, const Event& event) override;
        virtual bool AllowExecution(Action* action, const Event& event) override;
        virtual void After(Action* action, bool executed, const Event& event) override;
        virtual bool OverrideResult(Action* action, bool executed, const Event& event) override;

    public:
        void Add(ActionExecutionListener* listener)
        {
            listeners.push_back(listener);
        }
        void Remove(ActionExecutionListener* listener)
        {
            listeners.remove(listener);
        }

    private:
        std::list<ActionExecutionListener*> listeners;
    };

    // -----------------------------------------------------------------------------------------------------------------------

    enum ActionResult
    {
        ACTION_RESULT_UNKNOWN,
        ACTION_RESULT_OK,
        ACTION_RESULT_IMPOSSIBLE,
        ACTION_RESULT_USELESS,
        ACTION_RESULT_FAILED
    };

    class Engine : public PlayerbotAIAware
    {
    public:
        Engine(PlayerbotAI* ai, AiObjectContext *factory, BotState state);

	    void Init();
        void addStrategy(const std::string& name);
		void addStrategies(std::string first, ...);
        bool removeStrategy(const std::string& name, bool init = true);
        bool HasStrategy(const std::string& name);
        Strategy* GetStrategy(const std::string& name) const;
        void removeAllStrategies();
        void toggleStrategy(const std::string& name);
        std::string ListStrategies();
        std::list<std::string_view> GetStrategies();
		bool ContainsStrategy(StrategyType type);
		void ChangeStrategy(const std::string& names);
		void PrintStrategies(Player* requester, const std::string& engineType);
        std::string GetLastAction() { return lastAction; }
        const Action* GetLastExecutedAction() const { return lastExecutedAction; }

    public:
	    virtual bool DoNextAction(Unit*, int depth, bool minimal, bool isStunned);
	    ActionResult ExecuteAction(const std::string& name, Event& event);
        // Queue a named mature action on this engine so its normal
        // prerequisite/continuation chain runs on the next AI tick. This is
        // intentionally a thin queue entry, not a second movement/combat path.
        bool QueueAction(const std::string& name, float relevance, const Event& event);
        bool CanExecuteAction(const std::string& name, bool isUseful = true, bool isPossible = true);

    public:
        void AddActionExecutionListener(ActionExecutionListener* listener)
        {
            actionExecutionListeners.Add(listener);
        }
        void removeActionExecutionListener(ActionExecutionListener* listener)
        {
            actionExecutionListeners.Remove(listener);
        }

    public:
	    virtual ~Engine(void);

    protected:
        bool MultiplyAndPush(NextAction** actions, float forceRelevance, bool skipPrerequisites, const Event& event, const char* pushType);
        bool MultiplyAndPush(const std::vector<NextAction>& actions, float forceRelevance, bool skipPrerequisites, const Event& event, const char* pushType);
        bool Reset();
        void ProcessTriggers(bool minimal);
        void PushDefaultActions();
        void PushAgain(ActionNode* actionNode, float relevance, const Event& event);
        ActionNode* CreateActionNode(const std::string& name);
        virtual Action* InitializeAction(ActionNode* actionNode);
        virtual bool ListenAndExecute(Action* action, Event& event);
        // Issue #84: bounded failure backoff + transition invalidation.
        // Only autonomous background work is throttled; owner commands,
        // reactions and combat go through untouched (AllowBackgroundRetry).
        bool AllowBackgroundRetry(Action* action, Event& event) const;
        std::string FailureKey(Action* action, Event& event, ActionResult result) const;
        bool IsFailureBackedOff(Action* action, Event& event) const;
        void RecordFailure(Action* action, Event& event, ActionResult result);
        void ClearActionFailures(Action* action, Event& event);
        void RefreshFailureContext();
        // Pop and delete every queued node without touching strategies,
        // triggers or multipliers (no Reset()/Init() interplay).
        void DrainQueue();

    private:
        void LogAction(const char* format, ...);
        void LogValues();
        // Ordered join of the currently attached strategy names. A strategy
        // change that leaves this unchanged is a no-op and must not call
        // Init(), because Init() -> Reset() empties the action queue.
        std::string StrategySignature() const;

    protected:
	    Queue queue;
	    std::list<TriggerNode*> triggers;
        std::list<Multiplier*> multipliers;
        AiObjectContext* aiObjectContext;
        std::map<std::string, Strategy*> strategies;
        float lastRelevance;
        std::string lastAction;
        ActionExecutionListeners actionExecutionListeners;
        BotState state;
        Action* lastExecutedAction;
        bool inDoNextAction = false;
        bool reinitPending = false;
        // Issue #84 state. Per-engine failure memory plus the transition
        // tracker used to invalidate stale work on arrival (any map).
        ActionFailureBackoff actionFailures;
        TransitionTracker transitions;
        float failX = 0.0f, failY = 0.0f, failZ = 0.0f;
        uint32_t failMoney = 0, failHealth = 0, failMana = 0;

    public:
        bool initMode = true;
    };
}
