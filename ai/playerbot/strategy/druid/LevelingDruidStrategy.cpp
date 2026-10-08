
#include "playerbot/playerbot.h"
#include "LevelingDruidStrategy.h"

using namespace ai;

NextAction** LevelingDruidStrategy::GetDefaultCombatActions()
{
    return NextAction::array(0, new NextAction("melee", ACTION_IDLE), NULL);
}

void LevelingDruidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    DruidStrategy::InitCombatTriggers(triggers);

    // Shapeshifted stand-down: a 10+ druid in Bear/Dire Bear/Cat already runs
    // a feral kit, so the caster wrath/moonfire/heal nodes below must not
    // outbid the form rotation. This node carries no action - it only wins
    // the relevance contest at HIGH while shifted, idling the kit.
    triggers.push_back(new TriggerNode(
        "in feral form",
        NextAction::array(0, new NextAction("melee", ACTION_HIGH), NULL)));

    // Cast Healing Touch when health is under 50% (critical health [0,20%) + low health [20,50%))
    triggers.push_back(new TriggerNode(
        "critical health",
        NextAction::array(0, new NextAction("healing touch", ACTION_CRITICAL_HEAL), NULL)));

    triggers.push_back(new TriggerNode(
        "low health",
        NextAction::array(0, new NextAction("healing touch", ACTION_CRITICAL_HEAL), NULL)));

    // Cast Rejuvenation when actually hurt (below the low-health line) with
    // mana to spare - the trigger gates both, so scratches no longer outbid
    // the DPS kit at MEDIUM_HEAL.
    triggers.push_back(new TriggerNode(
        "leveling rejuvenation",
        NextAction::array(0, new NextAction("rejuvenation", ACTION_MEDIUM_HEAL), NULL)));

    // Cast Wrath if initiating combat or the enemy is still at range
    triggers.push_back(new TriggerNode(
        "enemy out of melee",
        NextAction::array(0, new NextAction("wrath", ACTION_NORMAL), NULL)));

    // Close to melee like every other melee kit does. A druid below level 10 runs the
    // "leveling" kit, which owns neither "close" nor "ranged", so the generic
    // "enemy out of melee" -> "reach melee" rule is not in this engine at all - and the node
    // here used to hang on the trigger name "enemy out of melee range", which no creator
    // registers (only "enemy out of melee" does, TriggerContext.h), so it never fired:
    // tools/verify_action_trigger_wiring.py listed it as a dead-tree trigger and the druid had
    // no way to close or re-close a fight (a knocked-back or fleeing mob left it standing).
    // Reach outranks the wrath node above, so the druid walks in; wrath still lands whenever
    // reach cannot run - the bot rooted or stunned, or a target reach has given up on.
    triggers.push_back(new TriggerNode(
        "enemy out of melee",
        NextAction::array(0, new NextAction("reach melee", ACTION_MOVE), NULL)));

    // Cast Moonfire if not applied and mana is over 50%
    triggers.push_back(new TriggerNode(
        "leveling moonfire",
        NextAction::array(0, new NextAction("moonfire", ACTION_NORMAL + 1), NULL)));

    // Melee attack is the fallback via GetDefaultCombatActions()
}

void LevelingDruidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    DruidStrategy::InitNonCombatTriggers(triggers);

    // Cast Healing Touch out of combat while not at full health
    triggers.push_back(new TriggerNode(
        "critical health",
        NextAction::array(0, new NextAction("healing touch", ACTION_CRITICAL_HEAL), NULL)));

    triggers.push_back(new TriggerNode(
        "low health",
        NextAction::array(0, new NextAction("healing touch", ACTION_CRITICAL_HEAL), NULL)));

    triggers.push_back(new TriggerNode(
        "medium health",
        NextAction::array(0, new NextAction("healing touch", ACTION_CRITICAL_HEAL), NULL)));
}

void LevelingDruidPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    LevelingDruidStrategy::InitCombatTriggers(triggers);
    DruidPveStrategy::InitCombatTriggers(triggers);
}

void LevelingDruidPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    LevelingDruidStrategy::InitNonCombatTriggers(triggers);
    DruidPveStrategy::InitNonCombatTriggers(triggers);
}

void LevelingDruidPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    LevelingDruidStrategy::InitCombatTriggers(triggers);
    DruidPvpStrategy::InitCombatTriggers(triggers);
}

void LevelingDruidPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    LevelingDruidStrategy::InitNonCombatTriggers(triggers);
    DruidPvpStrategy::InitNonCombatTriggers(triggers);
}

void LevelingDruidRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    LevelingDruidStrategy::InitCombatTriggers(triggers);
    DruidRaidStrategy::InitCombatTriggers(triggers);
}

void LevelingDruidRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    LevelingDruidStrategy::InitNonCombatTriggers(triggers);
    DruidRaidStrategy::InitNonCombatTriggers(triggers);
}
