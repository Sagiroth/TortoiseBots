#pragma once
#include "playerbot/strategy/Value.h"
#include <string>

namespace ai
{
    class PatchwerkSoakerShieldValue : public UnitCalculatedValue
    {
    public:
        PatchwerkSoakerShieldValue(PlayerbotAI* ai) : UnitCalculatedValue(ai, "patchwerk soaker to shield") {}
        Unit* Calculate() override;
    };
    bool IsPatchwerkHealingAssignment(Player* player);
    Unit* RaidHealingAnchor(PlayerbotAI* ai);
    bool IsRaidHealerMaintenance(std::string name);
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
