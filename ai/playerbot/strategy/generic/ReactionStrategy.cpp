
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

    // Universal raid survival (any raid map): bomb carriers run 30yd clear,
    // non-tanks flank dragons, stacked ranged split. Reaction engine ticks
    // regardless of combat state. The legacy 4H threshold below only calls
    // generic hazard escape; it is not a mark rotation or tank assignment.
    triggers.push_back(new TriggerNode(
        "raid bomb debuff",
        NextAction::array(0, new NextAction("raid bomb runout", ACTION_EMERGENCY + 6), NULL)));

    triggers.push_back(new TriggerNode(
        "four horsemen mark",
        NextAction::array(0, new NextAction("move away from hazard", ACTION_EMERGENCY + 6), NULL)));

    triggers.push_back(new TriggerNode(
        "dragon breath risk",
        NextAction::array(0, new NextAction("dragon flank", ACTION_EMERGENCY + 4), NULL)));

    triggers.push_back(new TriggerNode(
        "raid spread needed",
        NextAction::array(0, new NextAction("raid spread", ACTION_EMERGENCY + 3), NULL)));
}