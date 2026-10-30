#include "playerbot/playerbot.h"
#include "BossTacticsStrategy.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"

using namespace ai;

void BossTacticsStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // An interrupt goes first: a Chain Lightning that lands costs more than a
    // target switch. The kill-first pick sits above the generic assist rows so
    // the add becomes the target before the assist re-pick runs.
    triggers.push_back(new TriggerNode(
        "boss tactics interrupt",
        NextAction::array(0, new NextAction("boss tactics interrupt", ACTION_INTERRUPT + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "boss tactics kill first",
        NextAction::array(0, new NextAction("boss tactics kill first", ACTION_HIGH + 1), NULL)));

    // A listed blast is being cast and this bot stands in it: the walk out comes first, above the generic step out of
    // a ground effect (avoid aoe, ACTION_EMERGENCY + 5), since the blast kills in one hit.
    triggers.push_back(new TriggerNode(
        "boss tactics avoid cast",
        NextAction::array(0, new NextAction("boss tactics avoid cast", ACTION_EMERGENCY + 6), NULL)));

    triggers.push_back(new TriggerNode(
        "boss tactics front",
        NextAction::array(0, new NextAction("boss tactics step behind", ACTION_HIGH), NULL)));

    // Leaving the map ends the strategy even from inside a fight.
    triggers.push_back(new TriggerNode(
        "leave boss tactics",
        NextAction::array(0, new NextAction("disable boss tactics strategy", 100.0f), NULL)));
}

void BossTacticsStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "leave boss tactics",
        NextAction::array(0, new NextAction("disable boss tactics strategy", 100.0f), NULL)));
}

void BossTacticsStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new BossTacticsHoldMultiplier(ai));
    multipliers.push_back(new BossTacticsCastHoldMultiplier(ai));
}
