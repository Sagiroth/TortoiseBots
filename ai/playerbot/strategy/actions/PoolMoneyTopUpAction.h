#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Action.h"

namespace ai
{
    // Level-up money top-up for masterless pool bots
    // (AiPlayerbot.PoolMoneyTopUp, runtime/PoolMoneyTopUpPolicy.h). Queued by
    // the "levelup" packet; lifts a broke bot to what its next class-trainer
    // visit costs, at most once per level.
    class PoolMoneyTopUpAction : public Action
    {
    public:
        PoolMoneyTopUpAction(PlayerbotAI* ai) : Action(ai, "pool money top up") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
        virtual bool isUsefulWhenStunned() override { return true; }
    };
}
