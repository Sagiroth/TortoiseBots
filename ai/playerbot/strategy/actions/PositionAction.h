#pragma once
#include "playerbot/PlayerbotAI.h"
#include "MovementActions.h"
#include "GenericActions.h"
#include "playerbot/strategy/values/PositionValue.h"

namespace ai
{
    // The stay walk is due now (runtime/StayArrivalPolicy.h StayWalkDue): its action's
    // isUseful and its trigger both ask this.
    bool StayWalkDueNow(PlayerbotAI* ai, Player* bot, PositionEntry const& stayPosition);
    // The point the stay walk goes to (the stay, or its point on a live boss anchor's circle); false: no floor there.
    bool StayWalkPoint(PlayerbotAI* ai, Player* bot, PositionEntry& stay);

    class PositionAction : public ChatCommandAction
    {
    public:
        PositionAction(PlayerbotAI* ai) : ChatCommandAction(ai, "position") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }
    };

    class MoveToPositionAction : public MovementAction
    {
    public:
        MoveToPositionAction(PlayerbotAI* ai, std::string name, std::string qualifier, bool idle = false) : MovementAction(ai, name), qualifier(qualifier), idle(idle) {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;

    protected:
        std::string qualifier;
        bool idle;
    };

    class GuardAction : public MoveToPositionAction
    {
    public:
        GuardAction(PlayerbotAI* ai) : MoveToPositionAction(ai, "move to position", "guard") {}
        virtual bool isUseful() override;
    };

    class SetReturnPositionAction : public Action
    {
    public:
        SetReturnPositionAction(PlayerbotAI* ai) : Action(ai, "set return position") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
        virtual bool isUsefulWhenStunned() override { return true; }
    };

    class ReturnAction : public MoveToPositionAction
    {
    public:
        ReturnAction(PlayerbotAI* ai) : MoveToPositionAction(ai, "return", "return", true) {}
        virtual bool isUseful() override;
    };

    class ReturnToStayPositionAction : public MoveToPositionAction
    {
    public:
        ReturnToStayPositionAction(PlayerbotAI* ai) : MoveToPositionAction(ai, "move to position", "stay") {}
        virtual bool isPossible() override;
        // The shared stay arrival rule and back-off (runtime/StayArrivalPolicy.h), the ones its trigger uses.
        virtual bool isUseful() override;
        virtual bool Execute(Event& event) override;
    };

    class ReturnToPullPositionAction : public MoveToPositionAction
    {
    public:
        ReturnToPullPositionAction(PlayerbotAI* ai) : MoveToPositionAction(ai, "return to pull position", "pull") {}
        virtual bool isPossible() override;
    };
}
