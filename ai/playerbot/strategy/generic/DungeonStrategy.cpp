
#include "playerbot/playerbot.h"
#include "DungeonStrategy.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"

using namespace ai;

// Multipliers are per engine, and every bot's combat engine carries this strategy: the trap hold
// lives here so an uncommanded bot's hold on a reach, a follow or a stay spot is not boss-tactics
// only. The avoid-aoe strategy carries its own copy for the engines it runs on; no engine gets both.
void DungeonStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new TrapHoldMultiplier(ai));
    // The charmed ally's area veto is the charm key's own, not the boss tactics': every bot carries this copy.
    multipliers.push_back(new CharmedAllyHoldMultiplier(ai));
}

void DungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // A taunt-immune boss on a non-tank (AiPlayerbot.DungeonRunImmuneBossVictimEscape): the victim escapes with its own
    // ability, else a paladin in reach protects it. Below the walk-outs and the bomb runout, above every heal and rotation.
    triggers.push_back(new TriggerNode(
        "immune boss victim",
        NextAction::array(0, new NextAction("immune boss victim escape", ACTION_EMERGENCY + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "immune boss victim protect",
        NextAction::array(0, new NextAction("bop immune boss victim", ACTION_EMERGENCY + 2), NULL)));

    // Custom Turtle raids (807/532/800) gate behind EnableCustomRaidTactics
    // in both paths, so a disabled custom gate cannot re-arm mid-fight.
    // Add this combat triggers in case the bot gets summoned into the dungeon and goes straight into combat

    // Data-driven boss tactics (ai_boss_tactic): the trigger holds the gate, see
    // BossTacticsPolicy.h. Off unless AiPlayerbot.DungeonTactics or a commander binding.
    triggers.push_back(new TriggerNode(
        "enter boss tactics",
        NextAction::array(0, new NextAction("enable boss tactics strategy", 100.0f), NULL)));

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
        "enter zul'gurub",
        NextAction::array(0, new NextAction("enable zul'gurub strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter ruins of ahn'qiraj",
        NextAction::array(0, new NextAction("enable ruins of ahn'qiraj strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter naxxramas",
        NextAction::array(0, new NextAction("enable naxxramas strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter ahn'qiraj temple",
        NextAction::array(0, new NextAction("enable ahn'qiraj temple strategy", 100.0f), NULL)));

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
    // Data-driven boss tactics (ai_boss_tactic): the trigger holds the gate, see
    // BossTacticsPolicy.h. Off unless AiPlayerbot.DungeonTactics or a commander binding.
    triggers.push_back(new TriggerNode(
        "enter boss tactics",
        NextAction::array(0, new NextAction("enable boss tactics strategy", 100.0f), NULL)));

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
        "enter zul'gurub",
        NextAction::array(0, new NextAction("enable zul'gurub strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "leave zul'gurub",
        NextAction::array(0, new NextAction("disable zul'gurub strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter ruins of ahn'qiraj",
        NextAction::array(0, new NextAction("enable ruins of ahn'qiraj strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "leave ruins of ahn'qiraj",
        NextAction::array(0, new NextAction("disable ruins of ahn'qiraj strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter naxxramas",
        NextAction::array(0, new NextAction("enable naxxramas strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "leave naxxramas",
        NextAction::array(0, new NextAction("disable naxxramas strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enter ahn'qiraj temple",
        NextAction::array(0, new NextAction("enable ahn'qiraj temple strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "leave ahn'qiraj temple",
        NextAction::array(0, new NextAction("disable ahn'qiraj temple strategy", 100.0f), NULL)));

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
