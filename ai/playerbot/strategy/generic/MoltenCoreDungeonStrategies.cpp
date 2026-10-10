
#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonStrategies.h"
#include "DungeonMultipliers.h"

using namespace ai;

void MoltenCoreDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "start magmadar fight",
        NextAction::array(0, new NextAction("enable magmadar fight strategy", 100.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "start golemagg fight",
        NextAction::array(0, new NextAction("enable golemagg fight strategy", 100.0f), NULL)));

    // Paladin resist auto-swap (fight-agnostic: the trigger reads boss
    // entries off the attacker list, so no per-boss wiring is needed).
    triggers.push_back(new TriggerNode(
        "boss wants fire aura",
        NextAction::array(0, new NextAction("swap fire resistance aura", ACTION_HIGH + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "boss wants shadow aura",
        NextAction::array(0, new NextAction("swap shadow resistance aura", ACTION_HIGH + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "start geddon fight",
        NextAction::array(0, new NextAction("enable geddon fight strategy", 100.0f), NULL)));
}

void GolemaggFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "fire protection potion ready",
        NextAction::array(0, new NextAction("fire protection potion", 100.0f), NULL)));

    // Tanks hold their camps while Trust is up; healers work the midpoint.
    if (ai->IsTank(ai->GetBot()))
    {
        triggers.push_back(new TriggerNode(
            "golemagg tank hold",
            NextAction::array(0, new NextAction("golemagg tank hold", ACTION_HIGH), NULL)));
    }

    triggers.push_back(new TriggerNode(
        "golemagg healer",
        NextAction::array(0, new NextAction("golemagg healer position", ACTION_MOVE + 5), NULL)));
}

void GolemaggFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end golemagg fight",
        NextAction::array(0, new NextAction("disable golemagg fight strategy", 100.0f), NULL)));
}

void GolemaggFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end golemagg fight",
        NextAction::array(0, new NextAction("disable golemagg fight strategy", 100.0f), NULL)));
}

void GolemaggFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "golemagg splash",
        NextAction::array(0, new NextAction("back off golemagg", ACTION_EMERGENCY + 5), NULL)));
}

void GolemaggFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new GolemaggFightMultiplier(ai));
}

void MoltenCoreDungeonStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "mc rune in sight",
        NextAction::array(0, new NextAction("move to mc rune", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "mc rune close",
        NextAction::array(0,
            new NextAction("douse mc rune eternal", 2.0f),
            new NextAction("douse mc rune aqual", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "fire protection potion ready",
        NextAction::array(0, new NextAction("fire protection potion", 100.0f), NULL)));
}

void MoltenCoreDungeonStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    // Dungeon-wide (not on the geddon fight strategy): the Living Bomb
    // carrier keeps approach suppression after Geddon dies and combat
    // ends — mirrors the donor's RaidMcStrategy registration.
    multipliers.push_back(new GeddonInfernoMultiplier(ai));
}

void MoltenCoreDungeonStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new GeddonInfernoMultiplier(ai));
}

void MagmadarFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    Player* bot = ai->GetBot();
    if (ai->IsRanged(bot) || ai->IsHeal(bot))
    {
        triggers.push_back(new TriggerNode(
            "magmadar too close",
            NextAction::array(0, new NextAction("move away from magmadar", 100.0f), NULL)));
    }

    triggers.push_back(new TriggerNode(
        "fire protection potion ready",
        NextAction::array(0, new NextAction("fire protection potion", 100.0f), NULL)));
}

void MagmadarFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end magmadar fight",
        NextAction::array(0, new NextAction("disable magmadar fight strategy", 100.0f), NULL)));
}

void MagmadarFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end magmadar fight",
        NextAction::array(0, new NextAction("disable magmadar fight strategy", 100.0f), NULL)));
}

void MagmadarFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "magmadar lava bomb",
        NextAction::array(0, new NextAction("move away from hazard", 100.0f), NULL)));
}

void MagmadarFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    Player* bot = ai->GetBot();
    if (ai->IsRanged(bot) || ai->IsHeal(bot))
    {
        multipliers.push_back(new PreventMoveAwayFromCreatureOnReachToCastMultiplier(ai));
    }
}

void GeddonFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "fire protection potion ready",
        NextAction::array(0, new NextAction("fire protection potion", 100.0f), NULL)));
}

void GeddonFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end geddon fight",
        NextAction::array(0, new NextAction("disable geddon fight strategy", 100.0f), NULL)));
}

void GeddonFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end geddon fight",
        NextAction::array(0, new NextAction("disable geddon fight strategy", 100.0f), NULL)));
}

void GeddonFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "geddon inferno",
        NextAction::array(0, new NextAction("move away from geddon", ACTION_EMERGENCY + 5), NULL)));
}

// No InitCombatMultipliers here: the multiplier lives on the dungeon-wide
// "molten core" strategy so bomb carriers stay covered after the fight.
