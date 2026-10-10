#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
// Zul'Gurub (Map 309, 20-man): transition skeleton only. Boss tactics
// are fresh designs (mod-playerbots has no ZG module); per-boss fight
// strategies hook into this strategy's combat triggers.
class ZulgurubDungeonStrategy : public Strategy
{
public:
    ZulgurubDungeonStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "zul'gurub"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
};

// Ruins of Ahn'Qiraj / AQ20 (Map 509, 20-man): transition + Ossirian
// crystal fight (see OssirianFightStrategy in a later PR). Other AQ20
// bosses have no donor tactics and arrive as their own fight strategies.
class RuinsOfAhnqirajDungeonStrategy : public Strategy
{
public:
    RuinsOfAhnqirajDungeonStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "ruins of ahn'qiraj"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
};

// Ahn'Qiraj Temple / AQ40 (Map 531, 40-man): transition skeleton only.
// Boss tactics are fresh designs (mod-playerbots has no AQ40 module).
class AhnqirajTempleDungeonStrategy : public Strategy
{
public:
    AhnqirajTempleDungeonStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "ahn'qiraj temple"; }

private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
};
}
