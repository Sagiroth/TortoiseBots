#include "playerbot/playerbot.h"
#include "KelthuzadDungeonStrategies.h"
#include "playerbot/strategy/actions/KelthuzadDungeonActions.h"

using namespace ai;

void KelthuzadFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Phase 1 + guardians: role-split add targeting.
    triggers.push_back(new TriggerNode(
        "kel'thuzad adds",
        NextAction::array(0, new NextAction("kel'thuzad choose target", ACTION_HIGH + 1), NULL)));

    // Phase 1: gather center when idled (position action no-ops
    // while the bot holds a target).
    triggers.push_back(new TriggerNode(
        "kel'thuzad phase one",
        NextAction::array(0, new NextAction("kel'thuzad position", ACTION_HIGH + 1), NULL)));

    // Phase 2: ring spots / tank anchors.
    triggers.push_back(new TriggerNode(
        "kel'thuzad phase two",
        NextAction::array(0, new NextAction("kel'thuzad position", ACTION_HIGH + 1), NULL)));

    // Shadow Fissure under the bot: 10yd clear, lethal relevance like
    // every sibling hazard (void zone, lava bomb).
    triggers.push_back(new TriggerNode(
        "kel'thuzad fissure",
        NextAction::array(0, new NextAction("move away from hazard", 100.0f), NULL)));
}

void KelthuzadFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end kel'thuzad fight",
        NextAction::array(0, new NextAction("disable kel'thuzad fight strategy", 100.0f), NULL)));
}

void KelthuzadFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new KelthuzadSuppressMultiplier(ai));
}

void KelthuzadFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end kel'thuzad fight",
        NextAction::array(0, new NextAction("disable kel'thuzad fight strategy", 100.0f), NULL)));
}
