#pragma once
#include "playerbot/strategy/Strategy.h"
#include "playerbot/strategy/Multiplier.h"
#include "playerbot/PullOpeningPolicy.h"

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
        // Ranged pull: the cast only opens the core's ranged auto-repeat, the
        // arrow flies a swing later (Unit::_UpdateAutoRepeatSpell) and any
        // other cast or movement cancels it. The pull action records the cast
        // here and waits for the target to show the shot before it completes.
        bool HasPullActionCast() const { return pullActionCastMs != 0; }
        uint32 GetPullActionCastAgeMs() const;
        void NotePullActionCast();
        void OnPullStarted();
        void OnPullActionCompleted();
        void OnPullEnded();
        ReactStates GetPetReactState() const { return petReactState; }
        void SetPetReactState(ReactStates reactState) { petReactState = reactState; }
        // Per-command mode: true while a pull/pullback command owns this pull.
        // The return leg follows the intent snapshotted by RequestPull, not the
        // strategy set at evaluation time.
        void BeginCommand(bool pullback, bool hadPullBackStrategy, uint32 joinDelaySeconds = 0, bool rangedOrder = false);
        bool IsCommandActive() const { return commandActive; }
        bool IsCommandPullback() const { return commandPullback; }
        bool IsCommandRangedOrder() const { return commandRangedOrder; }
        // How the commanded pull opens (decided on the first RequestPull):
        // wanted from range or not, and why. What the tank can actually do
        // about it is GetRangedBlocker / GetPullActionName.
        bool CommandWantsRanged() const { return commandWantsRanged; }
        PullOpeningReason GetOpeningReason() const { return openingReason; }
        bool IsTargetPathReachable() const { return pathReachable; }
        // Why the tank cannot fire its ranged weapon at `distance` yards
        // (negative = ignore the minimum range): no weapon, skill, shoot spell,
        // ammo, or the mob is inside the shot's dead zone. Weapon-class pulls
        // ("shoot") only; a spell pull has no weapon to miss.
        PullRangedBlocker GetRangedBlocker(float distance = -1.0f) const;
        bool IsPullBackIntent() const { return pullBackIntent; }
        bool HadPullBack() const { return hadPullBack; }
        uint32 GetCommandJoinDelay() const { return commandJoinDelay; }
        time_t GetReturnStartTime() const { return returnStartTime; }
        // What BeginCommand changes, so a command refused after it can put an
        // earlier pull's command state back exactly.
        struct CommandState
        {
            bool active;
            bool pullback;
            bool hadPullBack;
            uint32 joinDelay;
            time_t returnStartTime;
            bool rangedOrder;
            bool wantsRanged;
            PullOpeningReason reason;
            bool pathReachable;
        };
        CommandState SaveCommand() const
        {
            return { commandActive, commandPullback, hadPullBack, commandJoinDelay, returnStartTime,
                commandRangedOrder, commandWantsRanged, openingReason, pathReachable };
        }
        void RestoreCommand(CommandState const& state)
        {
            commandActive = state.active;
            commandPullback = state.pullback;
            hadPullBack = state.hadPullBack;
            commandJoinDelay = state.joinDelay;
            returnStartTime = state.returnStartTime;
            commandRangedOrder = state.rangedOrder;
            commandWantsRanged = state.wantsRanged;
            openingReason = state.reason;
            pathReachable = state.pathReachable;
        }

    private:
        void SetTarget(Unit* target);
        void DecideOpening(Unit* target);

        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;

    private:
        std::string pullActionName; //shoot
        std::string preActionName;
        bool pendingToStart;
        bool pullActionCompleted;
        time_t pullStartTime;
        // WorldTimer ms of the ranged pull cast this pull is waiting on, or 0.
        uint32 pullActionCastMs;
        ReactStates petReactState;
        // Per-command return mode: set by the pull/pullback command, restored
        // to the tank's default when the pull ends. Never sticky.
        bool commandPullback;
        bool commandActive;
        // The order said `ranged`; and the opening decided from it, the
        // config and the mob (DecideOpening). Both live and die with the command.
        bool commandRangedOrder;
        bool commandWantsRanged;
        PullOpeningReason openingReason;
        bool pathReachable;
        bool hadPullBack;
        // Return leg for the pull in flight (see RequestPull): the command's
        // mode, or the tank's own "pull back" for an automatic pull.
        bool pullBackIntent;
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

    // Zero the wait window of every bot the given tank's pull holds, so each
    // one's own "pull hold expired" trigger releases it (stay + follow) on its
    // next tick. Used whenever the pull will not bring the party in: the tank
    // died, the return leg timed out, or the pull ended without aggro.
    void ReleaseHeldPartyNow(Player* tank);
}

using ai::PullStrategy;
