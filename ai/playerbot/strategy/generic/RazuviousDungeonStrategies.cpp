#include "playerbot/playerbot.h"
#include "RazuviousDungeonStrategies.h"
#include "playerbot/strategy/actions/RazuviousDungeonActions.h"

using namespace ai;

void RazuviousFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "razuvious mind control",
        NextAction::array(0, new NextAction("razuvious mind control", ACTION_EMERGENCY + 5), NULL)));
}

void RazuviousFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end razuvious fight",
        NextAction::array(0, new NextAction("disable razuvious fight strategy", 100.0f), NULL)));
}

void RazuviousFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end razuvious fight",
        NextAction::array(0, new NextAction("disable razuvious fight strategy", 100.0f), NULL)));
}

void RazuviousFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new RazuviousFightMultiplier(ai));
}
