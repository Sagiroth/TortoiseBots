
#include "playerbot/playerbot.h"
#include "MeleeCombatStrategy.h"

using namespace ai;

void MeleeCombatStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "enemy out of melee",
        NextAction::array(0, new NextAction("reach melee", ACTION_MOVE), NULL)));

    // Party tank-face: the tank sidesteps so the held mob's front points
    // away from the party. Combat-only row, trigger-scoped to
    // real-player-master groups; pool bots never fire it.
    triggers.push_back(new TriggerNode(
        "tank face needed",
        NextAction::array(0, new NextAction("tank face away", ACTION_MOVE + 5), NULL)));

    // No "enemy too close for melee" -> "move out of enemy contact" row: the
    // donor (mod-playerbots MeleeCombatStrategy) dropped it. Mobs keep walking
    // into the player's hitbox, so it outranked the swings and melee bots
    // spent whole seconds stepping out instead of hitting (Oct 2026 roster
    // poll: warriors dancing for ~8 s at 50% health before dying).
}

void SetBehindCombatStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "not behind target",
        NextAction::array(0, new NextAction("set behind", ACTION_HIGH), NULL)));
}

void ChaseJumpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("jump::chase", static_cast<float>(ACTION_MOVE) + 9.0f), NULL)));
}

void ChaseJumpStrategy::InitCombatTriggers(std::list<TriggerNode *> &triggers)
{
    InitNonCombatTriggers(triggers);
}

void ChaseJumpStrategy::InitReactionTriggers(std::list<TriggerNode *> &triggers)
{
    InitNonCombatTriggers(triggers);
}
