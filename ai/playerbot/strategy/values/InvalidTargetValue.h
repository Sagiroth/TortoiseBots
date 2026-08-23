#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class InvalidTargetValue : public BoolCalculatedValue, public Qualified
	{
	public:
        InvalidTargetValue(PlayerbotAI* ai, std::string name = "invalid target") : BoolCalculatedValue(ai, name), Qualified() {}
        virtual bool Calculate() override;
    };
}
