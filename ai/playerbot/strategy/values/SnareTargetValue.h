#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class SnareTargetValue : public UnitCalculatedValue, public Qualified
	{
	public:
        SnareTargetValue(PlayerbotAI* ai) :
            UnitCalculatedValue(ai, "snare target"), Qualified() {}

    protected:
        virtual Unit* Calculate() override;
	};
}
