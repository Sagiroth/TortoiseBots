#pragma once
#include "playerbot/strategy/Value.h"
#include "playerbot/PlayerbotAI.h"
#include <utility>
#include <vector>

namespace ai
{
    struct GarrTankAssignment
    {
        bool active = false;
        ObjectGuid boss;
        std::vector<std::pair<ObjectGuid, ObjectGuid>> owners;
        std::vector<ObjectGuid> targets;
    };

    class GarrTankAssignmentValue : public CalculatedValue<GarrTankAssignment>
    {
    public:
        GarrTankAssignmentValue(PlayerbotAI* ai) : CalculatedValue(ai, "garr tank assignment", 2) {}
    protected:
        GarrTankAssignment Calculate() override;
    };

    bool IsGarrTankOptedIn(PlayerbotAI* ai);
    Unit* GetAssignedGarrTankTarget(PlayerbotAI* ai);
    bool IsGarrTankTargetExcluded(PlayerbotAI* ai, Unit* target);
}
