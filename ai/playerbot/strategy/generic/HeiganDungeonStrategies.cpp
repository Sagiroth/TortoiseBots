#include "playerbot/playerbot.h"
#include "HeiganDungeonStrategies.h"

using namespace ai;

void HeiganFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Dance: Plague Cloud up — move to the current safe section.
    triggers.push_back(new TriggerNode(
        "heigan dance",
        NextAction::array(0, new NextAction("heigan dance move", ACTION_HIGH + 1), NULL)));

    // Fight: ranged/healers hold the platform (eruptions only hit floor).
    triggers.push_back(new TriggerNode(
        "heigan platform hold",
        NextAction::array(0, new NextAction("heigan hold platform", ACTION_HIGH), NULL)));
}

void HeiganFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end heigan fight",
        NextAction::array(0, new NextAction("disable heigan fight strategy", 100.0f), NULL)));
}

void HeiganFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end heigan fight",
        NextAction::array(0, new NextAction("disable heigan fight strategy", 100.0f), NULL)));
}
