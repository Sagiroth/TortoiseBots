#pragma once
#include "playerbot/strategy/Strategy.h"
#include "playerbot/strategy/Multiplier.h"

namespace ai
{
    class PullStrategy : public Strategy
    {
    public:
        PullStrategy(PlayerbotAI* ai, std::string pullAction, std::string prePullAction = "");

    public:
        std::string getName() override { return "pull"; }

        static PullStrategy* Get(PlayerbotAI* ai);
        static uint8 GetMaxPullTime() { return 15; }
        const time_t& GetPullStartTime() const { return pullStartTime; }

        bool CanDoPullAction(Unit* target);

        Unit* GetTarget() const;
        bool HasTarget() const { return GetTarget() != nullptr; }

        virtual std::string GetPullActionName() const;
        std::string GetSpellName() const;
        float GetRange() const;

        virtual std::string GetPreActionName() const;

        void RequestPull(Unit* target, bool resetTime = true);
        bool IsPullPendingToStart() const { return pendingToStart; }
        bool HasPullStarted() const { return pullStartTime > 0; }
        bool HasPullActionCompleted() const { return pullActionCompleted; }
        void OnPullStarted();
        void OnPullActionCompleted();
        void OnPullEnded();
        ReactStates GetPetReactState() const { return petReactState; }
        void SetPetReactState(ReactStates reactState) { petReactState = reactState; }
        // Per-command mode: true while a pull/pullback command owns this pull.
        // The return leg follows commandPullback, not the sticky strategy.
        void BeginCommand(bool pullback, bool hadPullBackStrategy, uint32 joinDelaySeconds = 0);
        bool IsCommandActive() const { return commandActive; }
        bool IsCommandPullback() const { return commandPullback; }
        bool HadPullBack() const { return hadPullBack; }
        uint32 GetCommandJoinDelay() const { return commandJoinDelay; }
        time_t GetReturnStartTime() const { return returnStartTime; }

    private:
        void SetTarget(Unit* target);

        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;

    private:
        std::string pullActionName; //shoot
        std::string preActionName;
        bool pendingToStart;
        bool pullActionCompleted;
        time_t pullStartTime;
        ReactStates petReactState;
        // Per-command return mode: set by the pull/pullback command, restored
        // to the tank's default when the pull ends. Never sticky.
        bool commandPullback;
        bool commandActive;
        bool hadPullBack;
        uint32 commandJoinDelay;
        time_t returnStartTime;
    };

    class PullMultiplier : public Multiplier
    {
    public:
        PullMultiplier(PlayerbotAI* ai) : Multiplier(ai, "pull") {}

    public:
        float GetValue(Action* action) override;
    };

    class PossibleAdsStrategy : public Strategy
    {
    public:
        PossibleAdsStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "ads"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*> &triggers) override;
    };

    class PullBackStrategy : public Strategy
    {
    public:
        PullBackStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "pull back"; }

        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    };
}

using ai::PullStrategy;
