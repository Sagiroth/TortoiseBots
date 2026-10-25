#pragma once
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/actions/RagnarosActions.h"

namespace ai
{
    class RagnarosSonTrigger : public Trigger
    {
    public:
        RagnarosSonTrigger(PlayerbotAI* ai) : Trigger(ai, "ragnaros son pickup", 1) {}
        bool IsActive() override
        {
            Unit* target = GetRagnarosSonTarget(ai);
            return target && target != AI_VALUE(Unit*, "current target");
        }
    };
}
