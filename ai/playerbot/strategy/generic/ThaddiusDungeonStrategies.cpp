#include "playerbot/playerbot.h"
#include "ThaddiusDungeonStrategies.h"
#include "playerbot/strategy/actions/ThaddiusDungeonActions.h"

using namespace ai;

void ThaddiusFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Pet phase: burn the nearest live add (fake-dead adds excluded).
    triggers.push_back(new TriggerNode(
        "thaddius phase pet",
        NextAction::array(0, new NextAction("thaddius attack nearest pet", ACTION_HIGH + 1), NULL)));

    // Transition: adds down, Thaddius still shielded — jump to the floor.
    triggers.push_back(new TriggerNode(
        "thaddius phase transition",
        NextAction::array(0, new NextAction("thaddius move to platform", ACTION_HIGH + 1), NULL)));

    // Thaddius phase: polarity sides by charge aura.
    triggers.push_back(new TriggerNode(
        "thaddius phase thaddius",
        NextAction::array(0, new NextAction("thaddius move polarity", ACTION_HIGH + 1), NULL)));
}

void ThaddiusFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end thaddius fight",
        NextAction::array(0, new NextAction("disable thaddius fight strategy", 100.0f), NULL)));
}

void ThaddiusFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end thaddius fight",
        NextAction::array(0, new NextAction("disable thaddius fight strategy", 100.0f), NULL)));
}

void ThaddiusFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new ThaddiusEvenHpMultiplier(ai));
}
