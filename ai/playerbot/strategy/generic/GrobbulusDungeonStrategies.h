#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
// Grobbulus (entry 15931): ranged injection carriers go behind the
// boss, poison clouds stepped out of (mod-playerbots parity:
// NaxxStrategy.cpp Grobbulus rows, minus return-to-center and MT
// rotation — see policy header for why). Melee carriers are covered
// by the universal bomb runout (28169).
class GrobbulusFightStrategy : public Strategy
{
public:
    GrobbulusFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "grobbulus"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
};
}
