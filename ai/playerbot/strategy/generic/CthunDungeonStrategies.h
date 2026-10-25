#pragma once
#include "MoltenCoreDungeonStrategies.h"

namespace ai
{
    // Reuse the existing exclusion/retarget path without adding movement rules.
    class CthunCarapaceStrategy : public GolemaggDpsStrategy
    {
    public:
        CthunCarapaceStrategy(PlayerbotAI* ai) : GolemaggDpsStrategy(ai) {}
        std::string getName() override { return "cthun carapace"; }
    };
}
