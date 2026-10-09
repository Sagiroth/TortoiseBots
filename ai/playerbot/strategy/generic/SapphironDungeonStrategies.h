#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
// Sapphiron (entry 15989): air phase hides behind iceblocked players,
// blizzards (NPC 16474) stepped out of, melee work the flanks
// (mod-playerbots parity: NaxxStrategy.cpp Sapphiron rows, re-derived
// for vanilla hover instead of the donor's IsFlying).
class SapphironFightStrategy : public Strategy
{
public:
    SapphironFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "sapphiron"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
};
}
