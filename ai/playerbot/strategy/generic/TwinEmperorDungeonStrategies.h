#pragma once
#include "MoltenCoreDungeonStrategies.h"

namespace ai
{
    // Reuse the established DPS exclusion multiplier and retarget action.
    // This is an explicit physical-DPS assignment, never a tank assignment.
    class TwinPhysicalDpsStrategy : public GolemaggDpsStrategy
    {
    public:
        TwinPhysicalDpsStrategy(PlayerbotAI* ai) : GolemaggDpsStrategy(ai) {}
        std::string getName() override { return "twins physical dps"; }
    };
}
