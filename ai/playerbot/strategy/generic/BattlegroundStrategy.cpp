
#include "playerbot/playerbot.h"
#include "BattlegroundStrategy.h"

using namespace ai;

void BattlegroundStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "bg waiting",
        NextAction::array(0, new NextAction("bg move to start", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "player has flag",
        NextAction::array(0, new NextAction("jump::position bg objective", 3.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "bg active",
        NextAction::array(0, new NextAction("check mount state", 2.0f), new NextAction("bg move to objective", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("bg check objective", 10.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "bg active",
        NextAction::array(0, new NextAction("bg check flag", ACTION_HIGH), NULL)));

    triggers.push_back(new TriggerNode(
        "bg ended",
        NextAction::array(0, new NextAction("bg leave", ACTION_HIGH), NULL)));

    /*triggers.push_back(new TriggerNode(
        "enemy flagcarrier near",
        NextAction::array(0, new NextAction("attack enemy flag carrier", 80.0f), NULL)));*/

    /*triggers.push_back(new TriggerNode(
        "team flagcarrier near",
        NextAction::array(0, new NextAction("bg protect fc", 40.0f), NULL)));*/

    /*triggers.push_back(new TriggerNode(
        "player has flag",
        NextAction::array(0, new NextAction("bg move to objective", 90.0f), NULL)));*/
}

void WarsongStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "bg active",
        NextAction::array(0, new NextAction("bg check flag", 70.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "often",
        NextAction::array(0, new NextAction("bg use buff", 30.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "low health",
        NextAction::array(0, new NextAction("bg use buff", 30.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "low mana",
        NextAction::array(0, new NextAction("bg use buff", 30.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "enemy flagcarrier near",
        NextAction::array(0, new NextAction("attack enemy flag carrier", 80.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "player has flag",
        NextAction::array(0,
            new NextAction("jump::position bg objective", 80.5f),
            new NextAction("bg move to objective", 80.0f),
            NULL)));

    triggers.push_back(new TriggerNode(
        "player has flag",
        NextAction::array(0, new NextAction("rocket boots", 81.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("bg banner", 10.0f), NULL)));
}

void WarsongStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    InitNonCombatTriggers(triggers);
}

void AlteracStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
}

void AlteracStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("bg banner", ACTION_NORMAL), NULL)));
}

void ArathiStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "bg active",
        NextAction::array(0, new NextAction("bg check flag", 70.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "often",
        NextAction::array(0, new NextAction("bg use buff", 30.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "low health",
        NextAction::array(0, new NextAction("bg use buff", 30.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "low mana",
        NextAction::array(0, new NextAction("bg use buff", 30.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("bg banner", 10.0f), NULL)));
}

void ArathiStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    InitNonCombatTriggers(triggers);
}
