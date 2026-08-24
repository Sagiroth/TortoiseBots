#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class CombatStartTimeValue : public ManualSetValue<time_t>, public Qualified
	{
	public:
        CombatStartTimeValue(PlayerbotAI* ai) : ManualSetValue<time_t>(ai, 0), Qualified() {}
    };
}
