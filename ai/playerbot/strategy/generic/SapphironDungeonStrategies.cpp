#include "playerbot/playerbot.h"
#include "SapphironDungeonStrategies.h"

using namespace ai;

void SapphironFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Air phase: hide behind the nearest iceblocked player (Frost Breath
    // one-shots the open floor).
    triggers.push_back(new TriggerNode(
        "sapphiron air hide",
        NextAction::array(0, new NextAction("sapphiron hide", ACTION_HIGH + 2), NULL)));

    // Blizzard on the bot: step 10yd clear of the Blizzard NPC.
    triggers.push_back(new TriggerNode(
        "sapphiron blizzard",
        NextAction::array(0, new NextAction("sapphiron avoid blizzard", ACTION_HIGH + 1), NULL)));

    // Ground: non-tank melee work behind (tail sweep rear cone).
    triggers.push_back(new TriggerNode(
        "sapphiron flank",
        NextAction::array(0, new NextAction("set behind", ACTION_HIGH), NULL)));
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
