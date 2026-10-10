#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
// Instructor Razuvious (entry 16061), vanilla version (mod-playerbots parity:
// the 25-man branch of NaxxActions_Razuvious.cpp). There is no Obedience
// Crystal in 1.12: two priests Mind Control a Death Knight Understudy and
// tank the boss with it. While charmed, the priest does nothing else so the
// channel holds. Nobody else taunts the boss.
class RazuviousFightStrategy : public Strategy
{
public:
    RazuviousFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "razuvious"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
    void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
};
}
