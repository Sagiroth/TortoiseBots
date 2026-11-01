
#include "playerbot/playerbot.h"
#include "FollowMasterStrategy.h"

using namespace ai;

void FollowMasterStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "master target active",
        NextAction::array(0, new NextAction("dps assist", ACTION_EMERGENCY), NULL)));

    // Follow-first (owner report 2026-11-01): a master moving far enough to
    // follow overrides every non-combat row. Party buffs sit at
    // ACTION_NORMAL+2..+4 and drink/food at 6, so the dead-zone "update
    // follow" row at ACTION_IDLE always lost while a buff was pending and
    // the bot finished casting before running. The free-move catch-up row
    // fires only past the follow leash (or the dead zone for a tight leash),
    // where buff/drink must already stand down (FollowUrgencyPolicy.h), and
    // runs above every buff/downtime row; the idle station row keeps
    // ACTION_IDLE so a bot in place still buffs and drinks.
    triggers.push_back(new TriggerNode(
        "out of free move range",
        NextAction::array(0, new NextAction("follow", ACTION_HIGH), NULL)));

    triggers.push_back(new TriggerNode(
        "update follow",
        NextAction::array(0, new NextAction("follow", ACTION_IDLE), NULL)));
    // Idle station-keeping: a formation slot sits inside "update follow"'s
    // 5 yd dead zone, so without this a newly set formation (queue, circle,
    // shield, ...) visibly does nothing until the party moves. Fires while
    // the bot stands off its slot next to a still leader and walks it there.
    triggers.push_back(new TriggerNode(
        "formation station",
        NextAction::array(0, new NextAction("formation station", ACTION_IDLE), NULL)));
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
