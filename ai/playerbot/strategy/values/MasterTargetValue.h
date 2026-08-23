#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class MasterTargetValue : public UnitCalculatedValue
	{
	public:
        MasterTargetValue(PlayerbotAI* ai, std::string name = "master target") : UnitCalculatedValue(ai, name) {}

        virtual Unit* Calculate() override { return ai->GetGroupMaster(); }
    };
}
