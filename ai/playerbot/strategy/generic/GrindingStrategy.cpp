
#include "playerbot/playerbot.h"
#include "GrindingStrategy.h"

using namespace ai;

void GrindingStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "no target",
        NextAction::array(0,
        new NextAction("attack anything", 5.0f), NULL)));
    // Last resort under the prey rules (idle brief): a masterless bot with
    // no journey and no grind target at all drifts 50 yd to a near reachable
    // point instead of standing through whole parks. Below attack anything
    // (5.0) and every maintenance row, above the 0.5 idle floor - so it only
    // wins when nothing else wants the visit. The often trigger paces it:
    // seldom fires about once per 25 min per bot, so the drift never ran
    // live (0 rows in 10 pool snapshots); often fires about every 25 s, and
    // the wander gate (no journey, no grind target, can move) still vetoes
    // every tick where the bot has anything better to do.
    triggers.push_back(new TriggerNode(
        "often",
        NextAction::array(0,
        new NextAction("idle wander", 0.6f), NULL)));
}
