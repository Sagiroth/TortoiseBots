#pragma once
#include "playerbot/strategy/Trigger.h"

namespace ai
{
    class HasNearbyQuestTakerTrigger : public Trigger
    {
    public:
        HasNearbyQuestTakerTrigger(PlayerbotAI* ai) : Trigger(ai, "has nearby quest taker", 30) {}

        virtual bool IsActive() override;
    };

}
