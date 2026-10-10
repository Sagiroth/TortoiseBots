#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
// Thaddius (entry 15928): pet phase on Stalagg (15929) / Feugen (15930),
// platform transition, then polarity sides on Thaddius himself
// (mod-playerbots parity: NaxxStrategy.cpp Thaddius rows).
class ThaddiusFightStrategy : public Strategy
{
public:
    ThaddiusFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "thaddius"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
    void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
};
}
