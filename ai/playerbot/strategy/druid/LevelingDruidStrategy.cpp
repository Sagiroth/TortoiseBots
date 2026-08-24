
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

    // Cast Healing Touch when health is under 50% (critical health [0,20%) + low health [20,50%))
    triggers.push_back(new TriggerNode(
        "critical health",
        NextAction::array(0, new NextAction("healing touch", ACTION_CRITICAL_HEAL), NULL)));

    triggers.push_back(new TriggerNode(
        "low health",
        NextAction::array(0, new NextAction("healing touch", ACTION_CRITICAL_HEAL), NULL)));

    // Cast Rejuvenation if health is not full and Rejuvenation is not already applied
    triggers.push_back(new TriggerNode(
        "leveling rejuvenation",
        NextAction::array(0, new NextAction("rejuvenation", ACTION_MEDIUM_HEAL), NULL)));

    // Cast Wrath if initiating combat or the enemy is still at range
    triggers.push_back(new TriggerNode(
        "enemy out of melee range",
        NextAction::array(0, new NextAction("wrath", ACTION_NORMAL), NULL)));

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
