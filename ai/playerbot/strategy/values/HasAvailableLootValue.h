#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/LootObjectStack.h"

namespace ai
{
    class HasAvailableLootValue : public BoolCalculatedValue
	{
	public:
        HasAvailableLootValue(PlayerbotAI* ai) : BoolCalculatedValue(ai) {}

    public:
        virtual bool Calculate() override
        {
            return !AI_VALUE(bool, "can loot") &&
                    AI_VALUE(LootObjectStack*, "available loot")->CanLoot(sPlayerbotAIConfig.lootDistance);
        }
    };
}
