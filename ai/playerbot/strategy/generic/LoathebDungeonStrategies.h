#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
// Loatheb (entry 16011): spore assignment + tank/ranged spots only.
// No heal-timing design here: vanilla has no Necrotic Aura (the
// donor's LoathebGenericMultiplier does not transfer).
class LoathebFightStrategy : public Strategy
{
public:
    LoathebFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "loatheb"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
};
}
