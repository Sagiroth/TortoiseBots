#pragma once
#include "playerbot/strategy/Value.h"
#include "playerbot/PlayerbotAI.h"
#include <vector>

namespace ai
{
    struct GolemaggTankAssignment
    {
        bool active = false;
        ObjectGuid boss;
        std::vector<ObjectGuid> targets;
    };

    class GolemaggTankAssignmentValue : public CalculatedValue<GolemaggTankAssignment>
    {
    public:
        GolemaggTankAssignmentValue(PlayerbotAI* ai) : CalculatedValue(ai, "golemagg tank assignment", 2) {}
    protected:
        GolemaggTankAssignment Calculate() override;
    };

    bool IsGolemaggTankTargetExcluded(PlayerbotAI* ai, Unit* target);
}
