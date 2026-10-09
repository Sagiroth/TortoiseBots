#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
// Kel'Thuzad (entry 15990): phase 1 add priorities by role around the
// center, phase 2 ring + fissure flee + tank spots, Detonate Mana via
// the universal bomb runout (mod-playerbots parity: NaxxStrategy.cpp
// Kel'Thuzad rows, vanilla-adjusted).
class KelthuzadFightStrategy : public Strategy
{
public:
    KelthuzadFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "kel'thuzad"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
};
}
