#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
// Ossirian crystal fight (mod-playerbots parity: Aq20Strategy). The AQ20
// transition strategy lives in ClassicRaidDungeonStrategies.h.
class OssirianFightStrategy : public Strategy
{
public:
    OssirianFightStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "ossirian"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitDeadTriggers(std::list<TriggerNode*>& triggers) override;
};
}
