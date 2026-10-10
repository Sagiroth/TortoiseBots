#include "playerbot/playerbot.h"
#include "BlackwingLairDungeonStrategies.h"
#include "DungeonMultipliers.h"

using namespace ai;

void BlackwingLairDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "suppression device close",
        NextAction::array(0, new NextAction("disarm suppression device", 80.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "start razorgore fight",
        NextAction::array(0, new NextAction("enable razorgore fight strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "boss wants fire aura",
        NextAction::array(0, new NextAction("swap fire resistance aura", ACTION_HIGH + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "boss wants shadow aura",
        NextAction::array(0, new NextAction("swap shadow resistance aura", ACTION_HIGH + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "start broodlord fight",
        NextAction::array(0, new NextAction("enable broodlord fight strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "start nefarian fight",
        NextAction::array(0, new NextAction("enable nefarian fight strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "start vael fight",
        NextAction::array(0, new NextAction("enable vael fight strategy", 100.0f), NULL)));
}

void RazorgoreFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "fire protection potion ready",
        NextAction::array(0, new NextAction("fire protection potion", 100.0f), NULL)));

    // Off-tank engage (donor MarkBoss attack arm): tanks attack Razorgore
    // while eggs live so the boss is held from the pull. Non-tanks never
    // see this node; the victim guard + egg check live in the action.
    if (ai->IsTank(ai->GetBot()))
    {
        triggers.push_back(new TriggerNode(
            "razorgore engage",
            NextAction::array(0, new NextAction("razorgore engage", 60.0f), NULL)));
    }
}

void RazorgoreFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end razorgore fight",
        NextAction::array(0, new NextAction("disable razorgore fight strategy", 100.0f), NULL)));
}

void RazorgoreFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end razorgore fight",
        NextAction::array(0, new NextAction("disable razorgore fight strategy", 100.0f), NULL)));
}

void RazorgoreFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "razorgore cone",
        NextAction::array(0, new NextAction("escape razorgore cone", ACTION_EMERGENCY + 5), NULL)));

    triggers.push_back(new TriggerNode(
        "razorgore ranged",
        NextAction::array(0, new NextAction("back off razorgore", ACTION_EMERGENCY + 4), NULL)));
}

void RazorgoreFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new RazorgoreOffTankMultiplier(ai));
}

void BroodlordFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    Player* bot = ai->GetBot();
    if (ai->IsRanged(bot) || ai->IsHeal(bot))
    {
        triggers.push_back(new TriggerNode(
            "broodlord ranged",
            NextAction::array(0, new NextAction("move away from broodlord", ACTION_EMERGENCY + 5), NULL)));
    }
}

void BroodlordFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end broodlord fight",
        NextAction::array(0, new NextAction("disable broodlord fight strategy", 100.0f), NULL)));
}

void BroodlordFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end broodlord fight",
        NextAction::array(0, new NextAction("disable broodlord fight strategy", 100.0f), NULL)));
}

void NefarianFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "nefarian wild magic",
        NextAction::array(0, new NextAction("ice block", ACTION_EMERGENCY + 5), NULL)));
}

void NefarianFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end nefarian fight",
        NextAction::array(0, new NextAction("disable nefarian fight strategy", 100.0f), NULL)));
}

void NefarianFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end nefarian fight",
        NextAction::array(0, new NextAction("disable nefarian fight strategy", 100.0f), NULL)));
}

void VaelFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "fire protection potion ready",
        NextAction::array(0, new NextAction("fire protection potion", 100.0f), NULL)));
}

void VaelFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end vael fight",
        NextAction::array(0, new NextAction("disable vael fight strategy", 100.0f), NULL)));
}

void VaelFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end vael fight",
        NextAction::array(0, new NextAction("disable vael fight strategy", 100.0f), NULL)));
}

void VaelFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    // Outranks the universal bomb runout (EMERGENCY+6): repulsion flee
    // with victim hold replaces the blind anchor-flee while +vael lives.
    triggers.push_back(new TriggerNode(
        "raid bomb debuff",
        NextAction::array(0, new NextAction("vael adrenaline flee", ACTION_EMERGENCY + 7), NULL)));

}

void BlackwingLairDungeonStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "suppression device need stealth",
        NextAction::array(0, new NextAction("stealth for suppression device", ACTION_HIGH + 3), NULL)));

    triggers.push_back(new TriggerNode(
        "suppression device in sight",
        NextAction::array(0, new NextAction("move to suppression device", ACTION_HIGH + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "suppression device close",
        NextAction::array(0, new NextAction("disarm suppression device", ACTION_HIGH + 4), NULL)));
}

class SuppressionRoomPassiveMultiplier : public Multiplier
{
public:
    SuppressionRoomPassiveMultiplier(PlayerbotAI* ai) : Multiplier(ai, "suppression room passive") {}

    float GetValue(Action* action) override
    {
        if (!action)
            return 1.0f;

        if (ai->GetBot()->GetClass() != CLASS_ROGUE)
            return 1.0f;

        const std::string& name = action->GetName();

        // Enable only the following strats for suppression room to avoid regular combat breaking logic
        if (name == "stealth for suppression device" ||
            name == "move to suppression device" ||
            name == "disarm suppression device" ||
            name == "deactivate suppression device")
        {
            return 1.0f;
        }

        if (name == "stealth" ||
            name == "unstealth" ||
            name == "check stealth" ||
            name == "sprint" ||
            name == "vanish")
        {
            return 1.0f;
        }

        if (name == "co" ||
            name == "nc" ||
            name == "load ai" ||
            name == "save ai" ||
            name == "list ai" ||
            name == "reset ai" ||
            name == "reset strats" ||
            name == "reset values" ||
            name == "check mount state" ||
            name == "accept invitation" ||
            name == "set combat state" ||
            name == "set non combat state" ||
            name == "set dead state" ||
            name == "update pvp strats" ||
            name == "update pve strats" ||
            name == "update raid strats" ||
            name == "loot roll" ||
            name == "auto loot roll" ||
            name == "follow" ||
            name == "stay" ||
            name == "food" ||
            name == "drink")
        {
            return 1.0f;
        }

        return 0.0f;
    }
};

void SuppressionRoomStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "suppression device need stealth",
        NextAction::array(0, new NextAction("vanish", ACTION_EMERGENCY + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "suppression device in sight",
        NextAction::array(0, new NextAction("move to suppression device", ACTION_HIGH + 8), NULL)));

    triggers.push_back(new TriggerNode(
        "suppression device close",
        NextAction::array(0, new NextAction("disarm suppression device", 90.0f), NULL)));
}

void SuppressionRoomStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "suppression device need stealth",
        NextAction::array(0, new NextAction("stealth for suppression device", ACTION_MOVE), NULL)));

    triggers.push_back(new TriggerNode(
        "suppression device in sight",
        NextAction::array(0, new NextAction("move to suppression device", ACTION_HIGH + 8), NULL)));

    triggers.push_back(new TriggerNode(
        "suppression device close",
        NextAction::array(0, new NextAction("disarm suppression device", ACTION_MOVE + 2), NULL)));
}

void SuppressionRoomStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new SuppressionRoomPassiveMultiplier(ai));
}

void SuppressionRoomStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new SuppressionRoomPassiveMultiplier(ai));
}

void SuppressionRoomStrategy::OnStrategyAdded(BotState state)
{
    if (ai->GetBot()->GetClass() == CLASS_ROGUE)
    {
        ai->ChangeStrategy("-avoid aoe", BotState::BOT_STATE_COMBAT);
        ai->ChangeStrategy("-avoid aoe", BotState::BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("-avoid aoe", BotState::BOT_STATE_REACTION);
        ai->ChangeStrategy("-avoid mobs", BotState::BOT_STATE_COMBAT);
        ai->ChangeStrategy("-avoid mobs", BotState::BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("-avoid mobs", BotState::BOT_STATE_REACTION);
    }
}