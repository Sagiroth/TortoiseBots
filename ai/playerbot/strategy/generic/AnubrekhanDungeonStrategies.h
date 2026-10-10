#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
// Anub'Rekhan (entry 15956): adds-first targeting for non-tanks,
// raid-to-center during Locust Swarm, flee suppressed while the swarm
// is up (mod-playerbots parity: NaxxStrategy.cpp Anub rows, minus the
// MT kite ring that needs live waypoints).
class AnubrekhanFightStrategy : public Strategy
{
public:
    AnubrekhanFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "anub'rekhan"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
    void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
};
}
