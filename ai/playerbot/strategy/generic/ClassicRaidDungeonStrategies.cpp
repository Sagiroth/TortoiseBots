
#include "playerbot/playerbot.h"
#include "ClassicRaidDungeonStrategies.h"

using namespace ai;

void ZulgurubDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    (void)triggers;
}

void ZulgurubDungeonStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    (void)triggers;
}

void RuinsOfAhnqirajDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "start ossirian fight",
        NextAction::array(0, new NextAction("enable ossirian fight strategy", 100.0f), NULL)));
}

void RuinsOfAhnqirajDungeonStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    (void)triggers;
}

void AhnqirajTempleDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    (void)triggers;
}

void AhnqirajTempleDungeonStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    (void)triggers;
}
