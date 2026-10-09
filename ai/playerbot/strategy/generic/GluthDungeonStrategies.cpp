#include "playerbot/playerbot.h"
#include "GluthDungeonStrategies.h"

using namespace ai;

void GluthFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Off-tank taunts when the MT holds 5+ Mortal Wounds.
    triggers.push_back(new TriggerNode(
        "gluth mortal wound swap",
        NextAction::array(0, new NextAction("taunt spell", ACTION_HIGH + 2), NULL)));

    // DPS: execute low chow, else boss.
    triggers.push_back(new TriggerNode(
        "gluth chow up",
        NextAction::array(0, new NextAction("gluth choose target", ACTION_HIGH + 1), NULL)));
}

void GluthFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end gluth fight",
        NextAction::array(0, new NextAction("disable gluth fight strategy", 100.0f), NULL)));
}

void GluthFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end gluth fight",
        NextAction::array(0, new NextAction("disable gluth fight strategy", 100.0f), NULL)));
}
