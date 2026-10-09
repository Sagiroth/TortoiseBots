#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
// Heigan the Unclean (entry 15936): ranged hold the platform during the
// fight; everyone dances the floor sections on Plague Cloud (29350).
// Timers and safe spots re-derived from the vanilla core script
// (boss_heigan.cpp), not the donor's WotLK constants.
class HeiganFightStrategy : public Strategy
{
public:
    HeiganFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "heigan"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
};
}
