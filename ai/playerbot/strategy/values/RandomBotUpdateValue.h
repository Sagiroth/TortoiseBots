#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class RandomBotUpdateValue : public ManualSetValue<bool>
	{
	public:
        RandomBotUpdateValue(PlayerbotAI* ai, std::string name = "random bot update") : ManualSetValue<bool>(ai, false, name) {}
    };
}
