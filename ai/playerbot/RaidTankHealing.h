#pragma once
#include "playerbot/strategy/Value.h"
#include <string>

namespace ai
{
    bool IsPatchwerkHealingAssignment(Player* player);
    bool IsRaidHealerFiller(std::string name);
    bool RaidHealerShouldHoldDamage(PlayerbotAI* ai);
    bool StopUnsafeRaidHealerFiller(PlayerbotAI* ai);

    class RaidTankHealingValue : public BoolCalculatedValue
    {
    public:
        RaidTankHealingValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "raid tank needs healing", 1) {}
        bool Calculate() override;
    };
}
