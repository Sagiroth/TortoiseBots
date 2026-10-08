
#include "playerbot/playerbot.h"
#include "FollowMasterStrategy.h"

using namespace ai;

void FollowMasterStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "master target active",
        NextAction::array(0, new NextAction("dps assist", ACTION_EMERGENCY), NULL)));

    // Out-of-combat catch-up runs below the party buffs (ACTION_NORMAL+2..+4):
    // donor follow is a 1.0 default action, so buffs always outbid it there.
    // When a buff is pending and its target is in range the buff wins this
    // tick; when the bot is far behind, the buff target is out of range, the
    // buff action is impossible and the engine falls through to follow here.
    // Combat keeps ACTION_HIGH: mid-fight positioning must never wait for a
    // buff. Still above loot (7) and drink (6), so a far-behind bot catches
    // up before looting.
    triggers.push_back(new TriggerNode(
        "out of free move range",
        NextAction::array(0, new NextAction("follow", ACTION_NORMAL), NULL)));

    triggers.push_back(new TriggerNode(
        "update follow",
        NextAction::array(0, new NextAction("follow", ACTION_IDLE), NULL)));
}

void FollowMasterStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "out of free move range",
        NextAction::array(0, new NextAction("follow", ACTION_HIGH), NULL)));

    triggers.push_back(new TriggerNode(
        "update follow",
        NextAction::array(0, new NextAction("follow", ACTION_IDLE), NULL)));
}

void FollowMasterStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    InitNonCombatTriggers(triggers);
}

void FollowMasterStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "stop follow",
        NextAction::array(0, new NextAction("stop follow", ACTION_PASSTROUGH), NULL)));
}

void FollowMasterStrategy::OnStrategyAdded(BotState state)
{
    if (state != BotState::BOT_STATE_REACTION)
    {
        ai->ChangeStrategy("+" + getName(), BotState::BOT_STATE_REACTION);
    }
}

void FollowMasterStrategy::OnStrategyRemoved(BotState state)
{
    if (state == ai->GetState() && ai->GetBot()->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
    {
        ai->StopMoving();
    }

    if (state == BotState::BOT_STATE_REACTION)
        return;

    bool hasFollow = false;

    for (uint8 checkState = (uint8)BotState::BOT_STATE_COMBAT; checkState < (uint8)BotState::BOT_STATE_REACTION; checkState++)
    {
        if (ai->HasStrategy(getName(), BotState(checkState)))
        {
            hasFollow = true;
            break;
        }
    }

    if (!hasFollow)
    {
        ai->ChangeStrategy("-" + getName(), BotState::BOT_STATE_REACTION);
    }
}
