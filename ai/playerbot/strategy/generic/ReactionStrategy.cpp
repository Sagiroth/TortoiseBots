
#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "ReactionStrategy.h"

using namespace ai;

void ReactionStrategy::InitReactionTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "combat start",
        NextAction::array(0, new NextAction("set combat state", ACTION_PASSTROUGH + 10), NULL)));

    triggers.push_back(new TriggerNode(
        "combat end",
        NextAction::array(0, new NextAction("set non combat state", ACTION_PASSTROUGH + 10), NULL)));

    triggers.push_back(new TriggerNode(
        "death",
        NextAction::array(0, new NextAction("set dead state", ACTION_PASSTROUGH + 10), NULL)));

    triggers.push_back(new TriggerNode(
        "resurrect",
        NextAction::array(0, new NextAction("set non combat state", ACTION_PASSTROUGH + 10), NULL)));

    // Notice environmental-damage traps (e.g. braziers) regardless of combat state - the
    // reaction engine ticks unconditionally, so this covers idle/wandering bots that would
    // otherwise stand in a hazard with no trigger watching for it.
    triggers.push_back(new TriggerNode(
        "environmental hazard nearby",
        NextAction::array(0, new NextAction("move away from hazard", ACTION_EMERGENCY + 5), NULL)));
}