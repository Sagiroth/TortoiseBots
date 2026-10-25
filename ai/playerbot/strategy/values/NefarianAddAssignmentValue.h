#pragma once
#include "playerbot/strategy/Value.h"
#include "playerbot/PlayerbotAI.h"
#include <utility>
#include <vector>

namespace ai
{
    struct NefarianAddAssignment
    {
        bool active = false;
        ObjectGuid boss;
        std::vector<std::pair<ObjectGuid, ObjectGuid>> owners;
        std::vector<ObjectGuid> targets;
    };

    class NefarianAddAssignmentValue : public CalculatedValue<NefarianAddAssignment>
    {
    public:
        // CalculatedValue divides the interval by two: 2 caches for one second.
        NefarianAddAssignmentValue(PlayerbotAI* ai) : CalculatedValue(ai, "nefarian add assignment", 2) {}
    protected:
        NefarianAddAssignment Calculate() override;
    };

    bool IsNefarianAddTankOptedIn(PlayerbotAI* ai);
    Unit* GetAssignedNefarianAddTarget(PlayerbotAI* ai, NefarianAddAssignment const& assignment);
    bool IsNefarianAddTargetExcluded(PlayerbotAI* ai, Unit* target);
}
