#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
// Gluth (entry 15932): mortal-wound taunt swap between tanks, chow
// execute triage for DPS (mod-playerbots parity: NaxxStrategy.cpp Gluth
// rows, minus the kite ring / decimate spots that need live coords).
class GluthFightStrategy : public Strategy
{
public:
    GluthFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "gluth"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
};
}
