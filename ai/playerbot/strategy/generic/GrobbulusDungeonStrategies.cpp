#include "playerbot/playerbot.h"
#include "GrobbulusDungeonStrategies.h"

using namespace ai;

void GrobbulusFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Poison cloud on top of the bot: step out. Tanks hold the boss
    // through clouds (Magmadar-style role gate at registration).
    if (!PlayerbotAI::IsTank(ai->GetBot()))
    {
        triggers.push_back(new TriggerNode(
            "grobbulus cloud",
            NextAction::array(0, new NextAction("move away from hazard", ACTION_HIGH + 1), NULL)));
    }
}

// Ranged carrier: behind the boss, not the generic 30yd runout.
// Reaction level at EMERGENCY+7, one above the universal bomb runout
// (96, which fires for the same 28169 aura and otherwise starves this):
// the reaction engine ticks before combat, so the combat-level row could
// never win. Melee carriers fall through to the universal runout.
void GrobbulusFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "grobbulus injection ranged",
        NextAction::array(0, new NextAction("grobbulus go behind", ACTION_EMERGENCY + 7), NULL)));
}

void GrobbulusFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end grobbulus fight",
        NextAction::array(0, new NextAction("disable grobbulus fight strategy", 100.0f), NULL)));
}

void GrobbulusFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end grobbulus fight",
        NextAction::array(0, new NextAction("disable grobbulus fight strategy", 100.0f), NULL)));
}
