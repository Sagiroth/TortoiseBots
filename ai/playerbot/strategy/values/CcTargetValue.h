#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "TargetValue.h"

namespace ai
{

    class CcTargetValue : public TargetValue, public Qualified
	{
	public:
        CcTargetValue(PlayerbotAI* ai, std::string name = "cc target") : TargetValue(ai, name), Qualified() {}

    public:
        Unit* Calculate() override;
    };
}
