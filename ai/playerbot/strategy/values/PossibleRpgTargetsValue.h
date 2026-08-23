#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "NearestUnitsValue.h"
#include "playerbot/PlayerbotAIConfig.h"

namespace ai
{
    class PossibleRpgTargetsValue : public NearestUnitsValue
	{
	public:
        PossibleRpgTargetsValue(PlayerbotAI* ai, float range = sPlayerbotAIConfig.rpgDistance);

    protected:
        void FindUnits(std::list<Unit*> &targets) override;
        bool AcceptUnit(Unit* unit) override;

    public:
        static std::vector<uint32> allowedNpcFlags;
	};
}
