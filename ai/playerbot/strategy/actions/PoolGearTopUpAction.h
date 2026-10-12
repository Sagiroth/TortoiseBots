#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Action.h"

namespace ai
{
    // Level-up gear top-up for masterless pool bots
    // (AiPlayerbot.PoolGearTopUpOnLevel, runtime/PoolGearTopUpPolicy.h).
    // Queued by the "levelup" packet and by the maintenance "equipment
    // audit", so a ding on a killing blow is caught out of combat; runs once
    // per level.
    class PoolGearTopUpAction : public Action
    {
    public:
        PoolGearTopUpAction(PlayerbotAI* ai) : Action(ai, "pool gear top up") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };
}
