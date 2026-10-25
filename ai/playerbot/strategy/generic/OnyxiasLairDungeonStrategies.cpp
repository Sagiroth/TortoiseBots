
#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonStrategies.h"
#include "playerbot/strategy/actions/OnyxiasLairDungeonActions.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"
#include "playerbot/strategy/actions/AttackAction.h"

using namespace ai;

void OnyxiasLairDungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "start onyxia fight",
        NextAction::array(0, new NextAction("enable onyxia fight strategy", 100.0f), NULL)));

    // Paladin resist auto-swap, as in Molten Core and Blackwing Lair:
    // Onyxia is on the fire list (Flame Breath, Fireball, Deep Breath).
    triggers.push_back(new TriggerNode(
        "boss wants fire aura",
        NextAction::array(0, new NextAction("swap fire resistance aura", ACTION_HIGH + 1), NULL)));

    // Boss protection potion (fire/shadow/nature per boss entry, see
    // ProtectionPotionPolicy.h): only once a listed boss is an attacker, and
    // below movement, dispels and every heal tier so survival comes first.
    triggers.push_back(new TriggerNode(
        "boss protection potion ready",
        NextAction::array(0, new NextAction("boss protection potion", ACTION_HIGH), NULL)));
}

void OnyxiaFightStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Phase 2 airborne: melee cannot reach; wands/shoot keep DPS on the
    // boss while whelp packs are the valid melee targets. Ranged spread
    // trims Fireball (18392) splash; universal flank keeps Deep Breath
    // lane clear via the reaction engine.
    // Retained upstream rationale; directional breath handling is separate below.
    // Existing opportunistic shooting/spread. This does not solve directional
    // Deep Breath; the optional air-spread strategy only reduces clustering.
    triggers.push_back(new TriggerNode(
        "onyxia airborne",
        NextAction::array(0, new NextAction("shoot", ACTION_HIGH),
                             new NextAction("raid spread", ACTION_HIGH - 1), NULL)));

    // Toggle (AiPlayerbot.OnyxiaWhelpsAndEggs, on by default; the triggers
    // stay silent while it is off). Phase 2 whelp duty for AoE-capable
    // ranged DPS, above the generic "shoot" row so the whelp becomes the
    // target before the wand fires.
    triggers.push_back(new TriggerNode(
        "onyxia whelps",
        NextAction::array(0, new NextAction("onyxia kill whelps", ACTION_HIGH + 1), NULL)));

    // Step out of an egg cave before its trap circle hatches whelps.
    triggers.push_back(new TriggerNode(
        "onyxia eggs near",
        NextAction::array(0, new NextAction("onyxia avoid eggs", ACTION_EMERGENCY), NULL)));
}

void OnyxiaFightStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new OnyxiaWhelpHoldMultiplier(ai));
}

void OnyxiaFightStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end onyxia fight",
        NextAction::array(0, new NextAction("disable onyxia fight strategy", 100.0f), NULL)));
}

void OnyxiaFightStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "end onyxia fight",
        NextAction::array(0, new NextAction("disable onyxia fight strategy", 100.0f), NULL)));
}

void OnyxiaFightStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    // Deep Breath outranks the generic flank: cast-triggered lane dodge to
    // the nearest safe zone beats cone geometry while a breath casts.
    triggers.push_back(new TriggerNode(
        "onyxia deep breath",
        NextAction::array(0, new NextAction("onyxia breath safe zone", ACTION_EMERGENCY + 5), NULL)));
}


namespace
{
class OnyxiaAirSpreadMultiplier : public Multiplier
{
public:
    OnyxiaAirSpreadMultiplier(PlayerbotAI* ai) : Multiplier(ai, "onyxia spread position") {}
    float GetValue(Action* action) override
    {
        if (!action || !OnyxiaAirSpreadEnabled(ai))
            return 1.0f;
        std::string const name = action->getName();
        if (name == "onyxia air spread" || name == "onyxia breath safe zone" ||
            name == "move away from hazard" || name == "flee" ||
            dynamic_cast<AttackAction*>(action))
            return 1.0f;
        // Keep the spread position while airborne; ordinary range/formation
        // movement resumes on landing. Casts from this position remain legal.
        return dynamic_cast<MovementAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action)
            ? 0.0f : 1.0f;
    }
};
}

void OnyxiaAirSpreadStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("onyxia air spread needed",
        NextAction::array(0, new NextAction("onyxia air spread", ACTION_EMERGENCY + 2), NULL)));
}

void OnyxiaAirSpreadStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new OnyxiaAirSpreadMultiplier(ai));
}

void OnyxiaAirSpreadStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}


namespace
{
class OnyxiaWhelpMovementMultiplier : public Multiplier
{
public:
    OnyxiaWhelpMovementMultiplier(PlayerbotAI* ai) : Multiplier(ai, "onyxia whelp movement") {}
    float GetValue(Action* action) override
    {
        if (!action || !OnyxiaWhelpSupportActive(ai))
            return 1.0f;
        Unit* boss = GetAirborneOnyxia(ai);
        if (!boss || action->GetTarget() != boss || ai->GetBot()->CanReachWithMeleeAutoAttack(boss) ||
            dynamic_cast<AttackAction*>(action))
            return 1.0f;
        // Turtle enlarges her airborne hitbox: permit legal melee specials
        // from the floor, but do not chase an unreachable airborne target.
        return dynamic_cast<ReachTargetAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action)
            ? 0.0f : 1.0f;
    }
};
}

void OnyxiaWhelpStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new OnyxiaWhelpMovementMultiplier(ai));
}

void OnyxiaWhelpStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}

namespace
{
class OnyxiaBreathMultiplier : public Multiplier
{
public:
    OnyxiaBreathMultiplier(PlayerbotAI* ai) : Multiplier(ai, "onyxia breath movement") {}
    float GetValue(Action* action) override
    {
        if (!action || !OnyxiaBreathActive(ai)) return 1.0f;
        std::string const name = action->getName();
        if (name == "onyxia deep breath escape" || name == "move away from hazard" || name == "raid bomb runout")
            return 1.0f;
        return dynamic_cast<MovementAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action) ? 0.0f : 1.0f;
    }
};
}
void OnyxiaBreathStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("onyxia breath escape needed",
        NextAction::array(0,new NextAction("onyxia deep breath escape",ACTION_EMERGENCY + 10),nullptr)));
}
void OnyxiaBreathStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers) { InitCombatTriggers(triggers); }
void OnyxiaBreathStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new OnyxiaBreathMultiplier(ai));
}
void OnyxiaBreathStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers) { InitCombatMultipliers(multipliers); }

void OnyxiaLandingTankStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("onyxia landing target needed",
        NextAction::array(0, new NextAction("onyxia landing attack", ACTION_HIGH + 2), nullptr)));
}
