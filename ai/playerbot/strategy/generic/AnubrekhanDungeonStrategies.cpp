#include "playerbot/playerbot.h"
#include "AnubrekhanDungeonStrategies.h"
#include "playerbot/strategy/actions/AnubrekhanDungeonActions.h"

using namespace ai;

void AnubrekhanFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Non-tanks burn guards first (lowest HP), boss when none stand.
    triggers.push_back(new TriggerNode(
        "anub'rekhan adds",
        NextAction::array(0, new NextAction("anub'rekhan choose target", ACTION_HIGH + 1), NULL)));

    // Locust Swarm: non-tanks collapse to the room center.
    triggers.push_back(new TriggerNode(
        "anub'rekhan swarm",
        NextAction::array(0, new NextAction("anub'rekhan to center", ACTION_HIGH + 2), NULL)));
}

void AnubrekhanFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end anub'rekhan fight",
        NextAction::array(0, new NextAction("disable anub'rekhan fight strategy", 100.0f), NULL)));
}

void AnubrekhanFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end anub'rekhan fight",
        NextAction::array(0, new NextAction("disable anub'rekhan fight strategy", 100.0f), NULL)));
}

void AnubrekhanFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new AnubrekhanSwarmMultiplier(ai));
}
