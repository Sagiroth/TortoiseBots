#include "playerbot/playerbot.h"
#include "ThaddiusDungeonStrategies.h"
#include "playerbot/strategy/actions/ThaddiusDungeonActions.h"

using namespace ai;

void ThaddiusFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Pet phase: burn the nearest live add (fake-dead adds excluded).
    // Lethal-fight relevance (100.0f, void-zone precedent): the targeting
    // holds above reach/flee so the split sticks; assists are suppressed
    // by the multiplier below instead of outbid.
    triggers.push_back(new TriggerNode(
        "thaddius phase pet",
        NextAction::array(0, new NextAction("thaddius attack nearest pet", 100.0f), NULL)));

    // Transition: adds down, Thaddius still shielded — drop to the floor.
    triggers.push_back(new TriggerNode(
        "thaddius phase transition",
        NextAction::array(0, new NextAction("thaddius move to platform", 100.0f), NULL)));

    // Thaddius phase: polarity sides by charge aura (held above generic
    // movement for the same reason).
    triggers.push_back(new TriggerNode(
        "thaddius phase thaddius",
        NextAction::array(0, new NextAction("thaddius move polarity", 100.0f), NULL)));
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
