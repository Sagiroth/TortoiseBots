#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
// Ruins of Ahn'Qiraj / AQ20 (Map 509, 20-man): transition strategy plus
// the Ossirian crystal fight (mod-playerbots parity: Aq20Strategy).
// Other AQ20 bosses have no donor tactics and arrive as their own fight
// strategies on this strategy's combat triggers.
class RuinsOfAhnqirajDungeonStrategy : public Strategy
{
public:
    RuinsOfAhnqirajDungeonStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "ruins of ahn'qiraj"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
};

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
