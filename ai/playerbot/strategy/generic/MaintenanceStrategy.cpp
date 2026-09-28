
#include "playerbot/playerbot.h"
#include "MaintenanceStrategy.h"

using namespace ai;

void MaintenanceStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "random",
        NextAction::array(0, new NextAction("clean quest log", 6.0f), NULL)));
    // Quest-log upkeep: when the log is nearly full AND something is
    // droppable, clean on a 5s timer instead of waiting for the 1/7 random
    // tick, so the bot makes room before the giver side starts refusing
    // (free slots < 5/10). Priority sits below travel (6.3-6.96) so a
    // doomed rescan never preempts movement; the trigger itself backs off
    // for five minutes after a no-op clean.
    triggers.push_back(new TriggerNode(
        "quest log nearly full",
        NextAction::array(0, new NextAction("clean quest log", 6.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "random",
        NextAction::array(0, new NextAction("use random recipe", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "random",
        NextAction::array(0, new NextAction("open random item", 0.9f), NULL)));

    triggers.push_back(new TriggerNode(
        "random",
        NextAction::array(0, new NextAction("disenchant random item", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "random",
        NextAction::array(0, new NextAction("enchant random item", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "random",
        NextAction::array(0, new NextAction("smart destroy item", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "move stuck",
        NextAction::array(0, new NextAction("unstuck", 0.7f), NULL)));

    triggers.push_back(new TriggerNode(
        "move long stuck",
        NextAction::array(0, new NextAction("unstuck", 0.9f), NULL)));

    // Periodic bag audit: re-check bags for upgrades every ~5 min, jittered
    // per bot so 500 bots do not run it on the same tick. The check interval
    // (300 s) is the jitter window: Trigger staggers first check by
    // rand() % interval, then needCheck() gates every 300 s after that.
    // Priority 5.0f matches the old equipment-audit slot (below travel and
    // quest work, above 0.7-1.0f upkeep). IsActive is always true; cheapness
    // comes from EquipUpgradesAction itself: with no EQUIP/BAD_EQUIP usage in
    // the bags the visitor returns nothing, no weapon means no MH/OH
    // unequip, and the run only clears expired usage values.
    triggers.push_back(new TriggerNode(
        "equipment audit",
        NextAction::array(0, new NextAction("equip upgrades", 5.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "random",
        NextAction::array(0, new NextAction("use random quest item", 0.9f), NULL)));

    triggers.push_back(new TriggerNode(
        "random",
        NextAction::array(0, new NextAction("auto share quest", 0.9f), NULL)));

    triggers.push_back(new TriggerNode(
        "random",
        NextAction::array(0, new NextAction("auto complete quest", 1.0f), NULL)));
}
