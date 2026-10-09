#include "playerbot/playerbot.h"
#include "GrobbulusDungeonStrategies.h"

using namespace ai;

void GrobbulusFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Ranged carrier: behind the boss, not just out.
    triggers.push_back(new TriggerNode(
        "grobbulus injection ranged",
        NextAction::array(0, new NextAction("grobbulus go behind", ACTION_HIGH + 2), NULL)));

    // Poison cloud on top of the bot: step out. Tanks hold the boss
    // through clouds (Magmadar-style role gate at registration).
    if (!PlayerbotAI::IsTank(ai->GetBot()))
    {
        triggers.push_back(new TriggerNode(
            "grobbulus cloud",
            NextAction::array(0, new NextAction("move away from hazard", ACTION_HIGH + 1), NULL)));
    }
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
