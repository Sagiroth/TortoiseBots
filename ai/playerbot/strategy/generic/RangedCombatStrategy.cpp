
#include "playerbot/playerbot.h"
#include "RangedCombatStrategy.h"

using namespace ai;

void RangedCombatStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "enemy too close for spell",
        NextAction::array(0, new NextAction("flee", ACTION_MOVE), NULL)));

    // Keep the casting band, both ways: "flee" above covers a target inside it,
    // this one closes the distance again when the target leaves it. Donor
    // mod-playerbots keeps the rule in its base CombatStrategy
    // (src/Ai/Base/Strategy/CombatStrategy.cpp); in this module the live class
    // kits chain through ClassStrategy and the ranged kit is the "ranged"
    // strategy, so the rule lives here and melee specs (which own a
    // higher-relevance "reach melee") are left untouched.
    triggers.push_back(new TriggerNode(
        "enemy out of spell",
        NextAction::array(0, new NextAction("reach spell", ACTION_HIGH), NULL)));
}
