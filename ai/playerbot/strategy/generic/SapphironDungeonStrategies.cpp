#include "playerbot/playerbot.h"
#include "SapphironDungeonStrategies.h"

using namespace ai;

void SapphironFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Air phase: hide behind the nearest iceblocked player (Frost Breath
    // one-shots the open floor). Lethal relevance: above all heals
    // (healers must stop casting and hide), same tier as bomb runout.
    triggers.push_back(new TriggerNode(
        "sapphiron air hide",
        NextAction::array(0, new NextAction("sapphiron hide", ACTION_EMERGENCY + 6), NULL)));

    // Blizzard on the bot: step clear of the Blizzard NPC (flank tier).
    triggers.push_back(new TriggerNode(
        "sapphiron blizzard",
        NextAction::array(0, new NextAction("sapphiron avoid blizzard", ACTION_EMERGENCY + 4), NULL)));
}

void SapphironFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end sapphiron fight",
        NextAction::array(0, new NextAction("disable sapphiron fight strategy", 100.0f), NULL)));
}

void SapphironFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end sapphiron fight",
        NextAction::array(0, new NextAction("disable sapphiron fight strategy", 100.0f), NULL)));
}
