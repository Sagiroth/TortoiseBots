
#include "playerbot/playerbot.h"
#include "EmeraldSanctumDungeonStrategies.h"

using namespace ai;

void EmeraldSanctumDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "start solnius fight",
        NextAction::array(0, new NextAction("enable solnius fight strategy", 100.0f), NULL)));
}

void EmeraldSanctumDungeonStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Dream-portal geometry (56500 teleports) needs live measurement;
    // out-of-combat behavior stays on the generic dungeon engine (#237).
}

void SolniusFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Acid Breath (24839) is a 30yd frontal cone: tanks hold the head away,
    // melee work the flanks via the universal dragon trigger. Emerald Rot
    // (56508) carriers run 30yd clear via the shared bomb trigger.
    triggers.push_back(new TriggerNode(
        "dragon breath risk",
        NextAction::array(0, new NextAction("dragon flank", ACTION_EMERGENCY + 4), NULL)));

    triggers.push_back(new TriggerNode(
        "solnius emerald rot",
        NextAction::array(0, new NextAction("raid bomb runout", ACTION_EMERGENCY + 6), NULL)));

    // Call of Nightmare (46079) spawns Suppressor 61212 / Scalebane 60746 /
    // Dragonkin 60743 / Wyrmkin 60745: DPS focus-fire the adds off the raid
    // (the trigger is DPS-only; tanks and healers keep their jobs).
    triggers.push_back(new TriggerNode(
        "solnius adds",
        NextAction::array(0, new NextAction("solnius attack add", ACTION_HIGH + 2), NULL)));
}

void SolniusFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end solnius fight",
        NextAction::array(0, new NextAction("disable solnius fight strategy", 100.0f), NULL)));
}

void SolniusFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end solnius fight",
        NextAction::array(0, new NextAction("disable solnius fight strategy", 100.0f), NULL)));
}

void SolniusFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "solnius emerald rot",
        NextAction::array(0, new NextAction("raid bomb runout", ACTION_EMERGENCY + 6), NULL)));
}
