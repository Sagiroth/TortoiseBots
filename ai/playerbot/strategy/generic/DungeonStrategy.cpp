
#include "playerbot/playerbot.h"
#include "DungeonStrategy.h"

using namespace ai;

void DungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Custom Turtle raids (807/532/800) gate behind EnableCustomRaidTactics
    // in both paths, so a disabled custom gate cannot re-arm mid-fight.
    // Add this combat triggers in case the bot gets summoned into the dungeon and goes straight into combat
    triggers.push_back(new TriggerNode(
        "enter onyxia's lair",
        NextAction::array(0, new NextAction("enable onyxia's lair strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter molten core",
        NextAction::array(0, new NextAction("enable molten core strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter blackwing lair",
        NextAction::array(0, new NextAction("enable blackwing lair strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter naxxramas",
        NextAction::array(0, new NextAction("enable naxxramas strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter ruins of ahn'qiraj",
        NextAction::array(0, new NextAction("enable ruins of ahn'qiraj strategy", 100.0f), NULL)));

    if (sPlayerbotAIConfig.enableCustomRaidTactics)
    {
        triggers.push_back(new TriggerNode(
            "enter emerald sanctum",
            NextAction::array(0, new NextAction("enable emerald sanctum strategy", 100.0f), NULL)));

        triggers.push_back(new TriggerNode(
            "enter lower karazhan",
            NextAction::array(0, new NextAction("enable lower karazhan strategy", 100.0f), NULL)));

        triggers.push_back(new TriggerNode(
            "enter karazhan crypt",
            NextAction::array(0, new NextAction("enable karazhan crypt strategy", 100.0f), NULL)));
    }

}

void DungeonStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "enter onyxia's lair",
        NextAction::array(0, new NextAction("enable onyxia's lair strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "leave onyxia's lair",
        NextAction::array(0, new NextAction("disable onyxia's lair strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter molten core",
        NextAction::array(0, new NextAction("enable molten core strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "leave molten core",
        NextAction::array(0, new NextAction("disable molten core strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter blackwing lair",
        NextAction::array(0, new NextAction("enable blackwing lair strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "leave blackwing lair",
        NextAction::array(0, new NextAction("disable blackwing lair strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter naxxramas",
        NextAction::array(0, new NextAction("enable naxxramas strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "leave naxxramas",
        NextAction::array(0, new NextAction("disable naxxramas strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter ruins of ahn'qiraj",
        NextAction::array(0, new NextAction("enable ruins of ahn'qiraj strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "leave ruins of ahn'qiraj",
        NextAction::array(0, new NextAction("disable ruins of ahn'qiraj strategy", 100.0f), NULL)));

    if (sPlayerbotAIConfig.enableCustomRaidTactics)
    {
        triggers.push_back(new TriggerNode(
            "enter emerald sanctum",
            NextAction::array(0, new NextAction("enable emerald sanctum strategy", 100.0f), NULL)));

        triggers.push_back(new TriggerNode(
            "leave emerald sanctum",
            NextAction::array(0, new NextAction("disable emerald sanctum strategy", 100.0f), NULL)));

        triggers.push_back(new TriggerNode(
            "enter lower karazhan",
            NextAction::array(0, new NextAction("enable lower karazhan strategy", 100.0f), NULL)));

        triggers.push_back(new TriggerNode(
            "leave lower karazhan",
            NextAction::array(0, new NextAction("disable lower karazhan strategy", 100.0f), NULL)));

        triggers.push_back(new TriggerNode(
            "enter karazhan crypt",
            NextAction::array(0, new NextAction("enable karazhan crypt strategy", 100.0f), NULL)));

        triggers.push_back(new TriggerNode(
            "leave karazhan crypt",
            NextAction::array(0, new NextAction("disable karazhan crypt strategy", 100.0f), NULL)));
    }

}
