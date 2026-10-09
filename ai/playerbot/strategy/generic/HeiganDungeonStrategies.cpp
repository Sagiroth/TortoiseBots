#include "playerbot/playerbot.h"
#include "HeiganDungeonStrategies.h"
#include "playerbot/strategy/actions/HeiganDungeonActions.h"

using namespace ai;

void HeiganFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Dance: current safe section. Lethal-mechanic relevance (100.0f,
    // local precedent): healing, formation, spread and flee must not
    // pull bots off the safe spot mid-dance.
    triggers.push_back(new TriggerNode(
        "heigan dance",
        NextAction::array(0, new NextAction("heigan dance move", 100.0f), NULL)));

    // Fight: ranged/healers hold the platform (eruptions only hit floor).
    triggers.push_back(new TriggerNode(
        "heigan platform hold",
        NextAction::array(0, new NextAction("heigan hold platform", 100.0f), NULL)));
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

void HeiganFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new HeiganDanceSuppressionMultiplier(ai));
}
