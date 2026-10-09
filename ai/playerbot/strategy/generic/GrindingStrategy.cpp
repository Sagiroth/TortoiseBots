
#include "playerbot/playerbot.h"
#include "GrindingStrategy.h"

using namespace ai;

void GrindingStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "no target",
        NextAction::array(0,
        new NextAction("attack anything", 5.0f), NULL)));
    // Idle drift beside the prey rules: a masterless bot with no journey
    // drifts 50 yd to a near reachable point instead of standing through
    // whole parks. Below attack anything (5.0) and every maintenance row,
    // above the 0.5 idle floor - so it only wins when nothing else wants the
    // visit. The often trigger paces it (about every 25 s); the wander gate
    // (no journey, can move) still vetoes every tick where the bot has
    // anything better to do. Not gated on the grind target: a held but
    // unattackable pick vetoed the only motion that could break the
    // standstill (0 wander rows for ~289 parked bots), while the attack row
    // still wins whenever the prey is usable.
    triggers.push_back(new TriggerNode(
        "often",
        NextAction::array(0,
        new NextAction("idle wander", 0.6f), NULL)));
}
