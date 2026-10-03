
#include "playerbot/playerbot.h"
#include "NonCombatStrategy.h"
#include "playerbot/strategy/Value.h"

using namespace ai;

void NonCombatStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("check mount state", 1.0f), new NextAction("check values", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "equipment audit",
        NextAction::array(0, new NextAction("equip upgrades", 5.0f), NULL)));
}

void CollisionStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "collision",
        NextAction::array(0, new NextAction("move out of collision", 2.0f), NULL)));
}

void MountStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    /*triggers.push_back(new TriggerNode(
        "no possible targets",
        NextAction::array(0, new NextAction("mount", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "no rpg target",
        NextAction::array(0, new NextAction("mount", 1.0f), NULL)));*/

    /*triggers.push_back(new TriggerNode(
        "often",
        NextAction::array(0, new NextAction("mount", 4.0f), NULL)));*/
}

void WorldBuffStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "need world buff",
        NextAction::array(0, new NextAction("world buff", 1.0f), NULL)));
}

void WorldBuffStrategy::OnStrategyRemoved(BotState state)
{
    // Remove world buffs
    Player* bot = ai->GetBot();
    if (bot)
    {
        for (auto& wb : sPlayerbotAIConfig.worldBuffs)
        {
            if (bot->HasAura(wb.spellId))
            {
                bot->RemoveAurasDueToSpell(wb.spellId);
            }
        }
    }
}

void NoWarStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "at war",
        NextAction::array(0, new NextAction("faction", 1.0f), NULL)));
}

void FishStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Side activity, never above levelling (review #402): the travel fish
    // errand keeps its 6.5 purpose weight; the open-water fallback only fires
    // while the bot is idle, and even then ranks below the quest hand-in
    // (6.36) and the grind errand (6.35) so it never pre-empts real work.
    triggers.push_back(new TriggerNode(
        "val::can fish",
        NextAction::array(0, new NextAction("move to fish" + modifier, 3.0f), new NextAction("fish" + modifier, 4.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "val::can open fishing dobber",
        NextAction::array(0, new NextAction("use fishing bobber", 99.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "val::done fishing",
        NextAction::array(0, new NextAction("equip upgrades", 6.0f), NULL)));
}
