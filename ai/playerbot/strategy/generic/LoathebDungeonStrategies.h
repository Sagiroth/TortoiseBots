#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
// Loatheb (entry 16011): spore assignment + tank/ranged spots, plus the
// assist/flee leg of the donor's LoathebGenericMultiplier (spore hold).
// No heal-timing design here: vanilla has no Necrotic Aura.
class LoathebFightStrategy : public Strategy
{
public:
    LoathebFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "loatheb"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
    void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
};
}
