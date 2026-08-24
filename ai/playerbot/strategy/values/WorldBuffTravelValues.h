#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class WorldBuffTravelStepValue : public ManualSetValue<uint8>
    {
    public:
        WorldBuffTravelStepValue(PlayerbotAI* ai)
            : ManualSetValue<uint8>(ai, 0, "world buff travel step") {}
    };
}