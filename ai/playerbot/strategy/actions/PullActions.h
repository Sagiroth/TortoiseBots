#pragma once
#include "playerbot/PlayerbotAI.h"
#include "GenericSpellActions.h"
#include "GenericActions.h"

namespace ai
{
    class PullRequestAction : public ChatCommandAction
    {
    public:
        PullRequestAction(PlayerbotAI* ai, std::string name) : ChatCommandAction(ai, name) {}

    protected:
        virtual bool Execute(Event& event) override;
        virtual Unit* GetTarget(Event& event) = 0;
    };

    class PullMyTargetAction : public PullRequestAction
    {
    public:
        PullMyTargetAction(PlayerbotAI* ai) : PullRequestAction(ai, "pull my target") {}

    private:
        Unit* GetTarget(Event& event) override;
    };

    class PullRTITargetAction : public PullRequestAction
    {
    public:
        PullRTITargetAction(PlayerbotAI* ai) : PullRequestAction(ai, "pull rti target") {}

    private:
        Unit* GetTarget(Event& event) override;
    };

    // Picks its own target instead of being handed one by a chat command, so a
    // tank can start a fight nobody asked it to start. Everything after the
    // target choice is the existing pull path.
    class PullNearestTargetAction : public PullRequestAction
    {
    public:
        PullNearestTargetAction(PlayerbotAI* ai) : PullRequestAction(ai, "pull nearest target") {}

        // Shared with ShouldPullTrigger: asking "is there anything to pull" and
        // "what do we pull" with two different pieces of code is how they drift
        // apart.
        static Unit* FindPullTarget(PlayerbotAI* ai);

    private:
        Unit* GetTarget(Event& event) override;
    };

    class PullStartAction : public Action
    {
    public:
        PullStartAction(PlayerbotAI* ai, std::string name = "pull start") : Action(ai, name) {}
        bool Execute(Event& event) override;
    };

    class PullAction : public CastSpellAction
    {
    public:
        PullAction(PlayerbotAI* ai, std::string name = "pull action");
        bool Execute(Event& event) override;
        bool isPossible() override;
        bool isUseful() override;
    private:
        void InitPullAction();
        std::string GetTargetName() override { return "pull target"; }
        std::string GetReachActionName() override { return "reach pull"; }
    };

    class PullEndAction : public Action
    {
    public:
        PullEndAction(PlayerbotAI* ai, std::string name = "pull end") : Action(ai, name) {}
        bool Execute(Event& event) override;
    };

    // Release a pull hold: drops the wait window, and with it our anchor copy
    // ("pull hold") and the stay it owns - never a stay the player placed
    // (that drops the hold marker first). Fires from the per-bot "pull hold
    // expired" trigger once the join window elapses, and from the tank's own
    // "pull anchor done" trigger once its anchor fight is over.
    class ReleasePullHoldAction : public Action
    {
    public:
        ReleasePullHoldAction(PlayerbotAI* ai, std::string name = "release pull hold") : Action(ai, name) {}
        bool Execute(Event& event) override;
    };
}
