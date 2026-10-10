
#include "playerbot/playerbot.h"
#include "ProtectionWarriorStrategy.h"

using namespace ai;

class ProtectionWarriorStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    ProtectionWarriorStrategyActionNodeFactory()
    {
        creators["charge"] = &charge;
        creators["sunder armor"] = &sunder_armor;
        creators["last stand"] = &last_stand;
        creators["taunt"] = &taunt;
        creators["berserker rage fear"] = &berserker_rage_fear;
    }

private:
    ACTION_NODE_A(charge, "charge", "reach melee");

    ACTION_NODE_A(sunder_armor, "sunder armor", "melee");

    ACTION_NODE_A(last_stand, "last stand", "intimidating shout");

    ACTION_NODE_A(taunt, "taunt", "battle shout taunt");

    ACTION_NODE_A(berserker_rage_fear, "berserker rage", "death wish");
};

ProtectionWarriorStrategy::ProtectionWarriorStrategy(PlayerbotAI* ai) : WarriorStrategy(ai)
{
    actionNodeFactories.Add(std::make_unique<ProtectionWarriorStrategyActionNodeFactory>());
}


NextAction** ProtectionWarriorStrategy::GetDefaultCombatActions()
{
    return NextAction::array(0, new NextAction("melee", ACTION_IDLE), NULL);
}

void ProtectionWarriorStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    WarriorStrategy::InitCombatTriggers(triggers);

    // Staggered defensives (donor TankWarriorStrategy): Shield Wall fires
    // early at low health (20-50%), Last Stand only at critical (<20%).
    // Stacking both at critical wastes Wall's 30-min cooldown on fights
    // Last Stand alone would survive.
    triggers.push_back(new TriggerNode(
        "low health",
        NextAction::array(0, new NextAction("shield wall", ACTION_MEDIUM_HEAL), NULL)));

    triggers.push_back(new TriggerNode(
        "critical health",
        NextAction::array(0, new NextAction("last stand", ACTION_EMERGENCY + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "has blessing of salvation",
        NextAction::array(0, new NextAction("remove blessing of salvation", ACTION_EMERGENCY), NULL)));

    triggers.push_back(new TriggerNode(
        "has greater blessing of salvation",
        NextAction::array(0, new NextAction("remove greater blessing of salvation", ACTION_EMERGENCY), NULL)));

    triggers.push_back(new TriggerNode(
        "enemy out of melee",
        NextAction::array(0, new NextAction("charge", ACTION_MOVE + 7), NULL)));

    triggers.push_back(new TriggerNode(
        "intercept and rage",
        NextAction::array(0, new NextAction("berserker stance", ACTION_MOVE + 6), NULL)));

    triggers.push_back(new TriggerNode(
        "intercept and rage",
        NextAction::array(0, new NextAction("intercept", ACTION_MOVE + 5), NULL)));

    // Intervene a focused party member (donor TankWarriorStrategy fires at
    // EMERGENCY): Intervene 45595 is a real learnable 1.18.1 spell
    // (Defensive-locked, taught by 47277), and the trigger/action/PROTECT
    // targeting were already registered — only this row was missing.
    triggers.push_back(new TriggerNode(
        "protect party member",
        NextAction::array(0, new NextAction("intervene", ACTION_EMERGENCY), NULL)));

    // Lost aggro (donor TankWarriorStrategy: taunt at INTERRUPT+1): a mob
    // peeling onto a non-tank member outranks DPS spenders and interrupts —
    // a missed kick costs damage, a missed taunt costs the healer.
    // Sits below EMERGENCY defensives, above pummel/shield-bash
    // (ACTION_INTERRUPT), every DPS row (HIGH band) and the out-of-melee
    // charge path (MOVE+7).
    triggers.push_back(new TriggerNode(
        "lose aggro",
        NextAction::array(0, new NextAction("taunt", ACTION_INTERRUPT + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "taunt on snare target",
        NextAction::array(0, new NextAction("taunt on snare target", ACTION_MOVE), NULL)));

    triggers.push_back(new TriggerNode(
        "demoralizing shout",
        NextAction::array(0, new NextAction("demoralizing shout", ACTION_HIGH + 6), NULL)));

    triggers.push_back(new TriggerNode(
        "shield block",
        NextAction::array(0, new NextAction("shield block", ACTION_HIGH + 5), NULL)));

    triggers.push_back(new TriggerNode(
        "sunder armor",
        NextAction::array(0, new NextAction("sunder armor", ACTION_HIGH + 2), NULL)));

    // Thunder clap on spare rage (donor TankWarriorStrategy fires it at
    // medium rage HIGH+1): the base AoE tree only covers thunder clap when
    // the opt-in aoe toggle is on, so a tank in a normal party pull never
    // clapped. Sits with sunder/revenge, below slam (rage-gated higher).
    triggers.push_back(new TriggerNode(
        "medium rage available",
        NextAction::array(0, new NextAction("thunder clap", ACTION_HIGH + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "revenge",
        NextAction::array(0, new NextAction("revenge", ACTION_HIGH + 3), NULL)));

    // Turtle Improved Shield Slam proc (donor's Sword-and-Board slot): a
    // +35%/+70% slam charge outranks the rage ladder but not tank
    // correctness — taunt (41) and shield block (tied 25, block listed
    // earlier wins ties) stay above it, matching the donor's taunt/block
    // above proc order. Still above baseline slam (24)/revenge/sunder.
    triggers.push_back(new TriggerNode(
        "improved shield slam proc",
        NextAction::array(0, new NextAction("shield slam", ACTION_HIGH + 5), NULL)));

    // Shield Slam at medium rage (donor TankWarriorStrategy gates at 40):
    // at 20 rage slam fired before the sunder stack was complete and stole
    // the GCD from revenge (5 rage, proc-gated). Thunder clap stays below
    // at HIGH+1 (matches donor slam+2/devastate+1/clap+1 shape).
    triggers.push_back(new TriggerNode(
        "medium rage available",
        NextAction::array(0, new NextAction("shield slam", ACTION_HIGH + 4), NULL)));

    triggers.push_back(new TriggerNode(
        "bloodthirst",
        NextAction::array(0, new NextAction("bloodthirst", ACTION_HIGH + 4), NULL)));

    triggers.push_back(new TriggerNode(
        "mortal strike",
        NextAction::array(0, new NextAction("mortal strike", ACTION_HIGH + 4), NULL)));

    triggers.push_back(new TriggerNode(
        "heroic strike",
        NextAction::array(0, new NextAction("heroic strike", ACTION_HIGH + 1), NULL)));

    // Disarm at HIGH+1 (donor TankWarriorStrategy): stripping the mob's
    // weapon cuts tank damage taken; buried at NORMAL it never fired.
    triggers.push_back(new TriggerNode(
        "disarm",
        NextAction::array(0, new NextAction("disarm", ACTION_HIGH + 1), NULL)));

    // Tortoise build additions:
    //
    // Concussion Blow capstone (Prot 6/1, TalentID 92): 5-sec stun + 100%
    // armor pen + rage gen on cast. Defining caster-interrupt + threat
    // ability. Same priority tier as Shield Slam (HIGH+4).
    triggers.push_back(new TriggerNode(
        "concussion blow",
        NextAction::array(0, new NextAction("concussion blow", ACTION_HIGH + 4), NULL)));

    // Improved Overpower stance-dance (Arms 2/1, 2/2): when an enemy dodges
    // our swing, swap to Battle Stance briefly, fire Overpower (near-100%
    // crit thanks to Improved Overpower 2/2), then swap back. Tactical
    // Mastery 5/5 keeps 25 rage across each switch so the trick is
    // rage-neutral. Crit triggers Deep Wounds bleed + Impale +20% crit dmg.
    //
    // OverpowerAvailableTrigger fires only when the dodge window is open.
    triggers.push_back(new TriggerNode(
        "overpower",
        NextAction::array(0, new NextAction("battle stance", ACTION_HIGH + 5),
                             new NextAction("overpower", ACTION_HIGH + 4),
                             new NextAction("defensive stance", ACTION_HIGH + 3), NULL)));

    // Bloodrage opener: Improved Bloodrage 2/2 gives +50% rage generation.
    // Cast on engage to jump-start the rotation (Shield Slam needs ~15 rage
    // to fire, Bloodrage gives 30+).
    triggers.push_back(new TriggerNode(
        "bloodrage",
        NextAction::array(0, new NextAction("bloodrage", ACTION_NORMAL + 5), NULL)));
}

void ProtectionWarriorStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    WarriorStrategy::InitNonCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "has blessing of salvation",
        NextAction::array(0, new NextAction("remove blessing of salvation", ACTION_EMERGENCY), NULL)));

    triggers.push_back(new TriggerNode(
        "has greater blessing of salvation",
        NextAction::array(0, new NextAction("remove greater blessing of salvation", ACTION_EMERGENCY), NULL)));
}

void ProtectionWarriorStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    WarriorStrategy::InitReactionTriggers(triggers);
}

void ProtectionWarriorStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    WarriorStrategy::InitDeadTriggers(triggers);
}

void ProtectionWarriorPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorStrategy::InitCombatTriggers(triggers);
    WarriorPveStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorStrategy::InitNonCombatTriggers(triggers);
    WarriorPveStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorPveStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorStrategy::InitReactionTriggers(triggers);
    WarriorPveStrategy::InitReactionTriggers(triggers);
}

void ProtectionWarriorPveStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorStrategy::InitDeadTriggers(triggers);
    WarriorPveStrategy::InitDeadTriggers(triggers);
}

void ProtectionWarriorPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorStrategy::InitCombatTriggers(triggers);
    WarriorPvpStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorStrategy::InitNonCombatTriggers(triggers);
    WarriorPvpStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorPvpStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorStrategy::InitReactionTriggers(triggers);
    WarriorPvpStrategy::InitReactionTriggers(triggers);
}

void ProtectionWarriorPvpStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorStrategy::InitDeadTriggers(triggers);
    WarriorPvpStrategy::InitDeadTriggers(triggers);
}

void ProtectionWarriorRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorStrategy::InitCombatTriggers(triggers);
    WarriorRaidStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorStrategy::InitNonCombatTriggers(triggers);
    WarriorRaidStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorRaidStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorStrategy::InitReactionTriggers(triggers);
    WarriorRaidStrategy::InitReactionTriggers(triggers);
}

void ProtectionWarriorRaidStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorStrategy::InitDeadTriggers(triggers);
    WarriorRaidStrategy::InitDeadTriggers(triggers);
}

void ProtectionWarriorAoeStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    WarriorAoeStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "melee medium aoe",
        NextAction::array(0, new NextAction("challenging shout", ACTION_HIGH + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "melee medium aoe",
        NextAction::array(0, new NextAction("battle shout taunt", ACTION_HIGH), NULL)));
}

void ProtectionWarriorAoeStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    WarriorAoeStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorAoePveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorAoeStrategy::InitCombatTriggers(triggers);
    WarriorAoePveStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorAoePveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorAoeStrategy::InitNonCombatTriggers(triggers);
    WarriorAoePveStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorAoePvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorAoeStrategy::InitCombatTriggers(triggers);
    WarriorAoePvpStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorAoePvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorAoeStrategy::InitNonCombatTriggers(triggers);
    WarriorAoePvpStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorAoeRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorAoeStrategy::InitCombatTriggers(triggers);
    WarriorAoeRaidStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorAoeRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorAoeStrategy::InitNonCombatTriggers(triggers);
    WarriorAoeRaidStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorBuffStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    WarriorBuffStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "defensive stance",
        NextAction::array(0, new NextAction("defensive stance", ACTION_MOVE), NULL)));

    triggers.push_back(new TriggerNode(
        "feared",
        NextAction::array(0, new NextAction("berserker rage fear", ACTION_INTERRUPT), NULL)));
}

void ProtectionWarriorBuffStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    WarriorBuffStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorBuffPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorBuffStrategy::InitCombatTriggers(triggers);
    WarriorBuffPveStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorBuffPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorBuffStrategy::InitNonCombatTriggers(triggers);
    WarriorBuffPveStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorBuffPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorBuffStrategy::InitCombatTriggers(triggers);
    WarriorBuffPvpStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorBuffPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorBuffStrategy::InitNonCombatTriggers(triggers);
    WarriorBuffPvpStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorBuffRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorBuffStrategy::InitCombatTriggers(triggers);
    WarriorBuffRaidStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "often",
        NextAction::array(0, new NextAction("stoneshield potion", ACTION_HIGH), NULL)));
}

void ProtectionWarriorBuffRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorBuffStrategy::InitNonCombatTriggers(triggers);
    WarriorBuffRaidStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorBoostStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    WarriorBoostStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorBoostStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    WarriorBoostStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorBoostPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorBoostStrategy::InitCombatTriggers(triggers);
    WarriorBoostPveStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorBoostPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorBoostStrategy::InitNonCombatTriggers(triggers);
    WarriorBoostPveStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorBoostPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorBoostStrategy::InitCombatTriggers(triggers);
    WarriorBoostPvpStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorBoostPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorBoostStrategy::InitNonCombatTriggers(triggers);
    WarriorBoostPvpStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorBoostRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorBoostStrategy::InitCombatTriggers(triggers);
    WarriorBoostRaidStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorBoostRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorBoostStrategy::InitNonCombatTriggers(triggers);
    WarriorBoostRaidStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorCcStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    WarriorCcStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "concussion blow",
        NextAction::array(0, new NextAction("concussion blow", ACTION_INTERRUPT), NULL)));

    triggers.push_back(new TriggerNode(
        "shield bash",
        NextAction::array(0, new NextAction("shield bash", ACTION_INTERRUPT), NULL)));

    triggers.push_back(new TriggerNode(
        "shield bash on enemy healer",
        NextAction::array(0, new NextAction("shield bash on enemy healer", ACTION_INTERRUPT), NULL)));
}

void ProtectionWarriorCcStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    WarriorCcStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorCcPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorCcStrategy::InitCombatTriggers(triggers);
    WarriorCcPveStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorCcPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorCcStrategy::InitNonCombatTriggers(triggers);
    WarriorCcPveStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorCcPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorCcStrategy::InitCombatTriggers(triggers);
    WarriorCcPvpStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorCcPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorCcStrategy::InitNonCombatTriggers(triggers);
    WarriorCcPvpStrategy::InitNonCombatTriggers(triggers);
}

void ProtectionWarriorCcRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorCcStrategy::InitCombatTriggers(triggers);
    WarriorCcRaidStrategy::InitCombatTriggers(triggers);
}

void ProtectionWarriorCcRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ProtectionWarriorCcStrategy::InitNonCombatTriggers(triggers);
    WarriorCcRaidStrategy::InitNonCombatTriggers(triggers);
}
