
#include "playerbot/playerbot.h"
#include "RuinsOfAhnqirajDungeonStrategies.h"

using namespace ai;

void OssirianFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "ossirian crystal run",
        NextAction::array(0, new NextAction("use ossirian crystal", 80.0f), NULL)));
}

void OssirianFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end ossirian fight",
        NextAction::array(0, new NextAction("disable ossirian fight strategy", 100.0f), NULL)));
}

void OssirianFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end ossirian fight",
        NextAction::array(0, new NextAction("disable ossirian fight strategy", 100.0f), NULL)));
}
