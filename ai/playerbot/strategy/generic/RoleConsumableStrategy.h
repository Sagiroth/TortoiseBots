#pragma once
#include "playerbot/strategy/Strategy.h"
namespace ai
{
class RoleConsumableStrategy : public Strategy
{
public:
    RoleConsumableStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "role consumables"; }
private:
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
};
}
