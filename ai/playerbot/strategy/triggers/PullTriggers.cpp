
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "PullTriggers.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/actions/PullActions.h"
#include "../../runtime/BotManager.h"
#include "../../runtime/PlayerbotAIStorage.h"

using namespace ai;

namespace
{
// Zero every held party bot's wait window so the per-bot "pull hold expired"
// trigger releases them on the next tick. Used when the tank's return leg
// times out: the pull ends, but the tank must not release the party itself.
void ReleasePullHoldNow(Player* tank)
{
    if (!tank)
        return;
    for (Player* member : LiveGroupMembers(tank->GetGroup()))
    {
        if (!member || member == tank || !TortoiseBots::BotManager::Instance().IsBot(member->GetObjectGuid()))
            continue;
        PlayerbotAI* memberAi = PlayerbotAIStorage::Instance().GetAI(member);
        if (!memberAi || !memberAi->GetAiObjectContext())
            continue;
        memberAi->GetAiObjectContext()->GetValue<uint8>("wait for attack time")->Set(0);
        memberAi->GetAiObjectContext()->GetValue<time_t>("combat start time")->Set(time(0));
    }
}
} // namespace

bool PullStartTrigger::IsActive()
{
    const PullStrategy* strategy = PullStrategy::Get(ai);
    return strategy && strategy->IsPullPendingToStart();
}

bool ShouldPullTrigger::IsActive()
{
    // Dungeons only, deliberately. Outdoors a bot already has grinding and travel
    // behaviour that this would compete with, and a pull that goes wrong out there
    // only adds to the death numbers. Inside, somebody has to start the fight or
    // the group stands around until a real player does it.
    Map* map = bot->GetMap();
    if (!map || !map->IsDungeon())
        return false;

    if (!PlayerbotAI::IsTank(bot))
        return false;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || !member->IsInWorld() || member->GetMapId() != bot->GetMapId())
            continue;

        // Never pull on top of a fight that is still running, and never onto a
        // corpse - somebody has to be raised first.
        if (member->IsInCombat() || !member->IsAlive())
            return false;

        // The healer decides the pace. Pulling with an empty healer is how a
        // group wipes on trash it could otherwise walk through.
        if (PlayerbotAI::IsHeal(member) && member->GetPowerType() == POWER_MANA)
        {
            const uint32 maxMana = member->GetMaxPower(POWER_MANA);
            if (maxMana && (100 * member->GetPower(POWER_MANA)) / maxMana < sPlayerbotAIConfig.mediumMana)
                return false;
        }
    }

    return PullNearestTargetAction::FindPullTarget(ai) != nullptr;
}

bool PullEndTrigger::IsActive()
{
    const PullStrategy* strategy = PullStrategy::Get(ai);
    if (!strategy || !strategy->HasPullStarted())
        return false;

    // The puller is down: the pull is over. End it and release the held party
    // right away - they have to fight for themselves, and a dead tank can
    // never satisfy the anchor condition below (the window would otherwise
    // run out its full length first).
    if (!bot->IsAlive())
    {
        ReleasePullHoldNow(bot);
        return true;
    }

    const time_t secondsSincePullStarted = time(0) - strategy->GetPullStartTime();
    // The return leg follows the intent recorded when the pull was requested
    // (command mode, or the tank's own "pull back" for an automatic pull).
    const bool pullback = strategy->IsPullBackIntent();

    if (pullback && strategy->HasPullActionCompleted())
    {
        PositionMap& posMap = AI_VALUE(PositionMap&, "position");
        PositionEntry pullPosition = posMap["pull"];
        if (!pullPosition.isSet() || pullPosition.mapId != bot->GetMapId())
            return true;

        // Bounded return: a stuck return (knockback, fear, path failure,
        // anchor in another map) ends the pull instead of holding the tank
        // inert forever. The clock starts when the pull lands. Ending the
        // pull does not release the held bots (they release themselves), so
        // zero their window here: the per-bot trigger fires on the next tick.
        time_t returnStart = strategy->GetReturnStartTime();
        if (returnStart > 0 && time(0) - returnStart >= static_cast<time_t>(sPlayerbotAIConfig.pullBackMaxReturnTime))
        {
            ReleasePullHoldNow(bot);
            return true;
        }

        // Tank back at the anchor: the pull ends and PullEndAction hands the
        // fight over (and re-stamps the held party to the join delay). Do not
        // discard the return anchor just because the target died, changed
        // victim, or the normal pull timeout elapsed while returning.
        return bot->GetDistance(pullPosition.x, pullPosition.y, pullPosition.z) <=
            ai->GetRange("follow");
    }

    Unit* target = strategy->GetTarget();
    if (!target || !target->IsInWorld() || !target->IsAlive())
        return true;

    if (secondsSincePullStarted >= strategy->GetMaxPullTime())
        return true;

    // An ordinary pull hands control back to combat as soon as its pull action
    // succeeds.  Before that, retain the request while the tank closes to its
    // class-specific melee/ranged pull distance.
    return strategy->HasPullActionCompleted();
}

bool PullHoldExpiredTrigger::IsActive()
{
    AiObjectContext* context = ai->GetAiObjectContext();
    if (!context)
        return false;
    PositionMap& posMap = AI_VALUE(PositionMap&, "position");
    if (!posMap["pull hold"].isSet())
        return false;
    // Early release already dropped the wait strategy: finish the cleanup.
    if (!ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT))
        return true;
    time_t combatStart = AI_VALUE(time_t, "combat start time");
    if (combatStart <= 0)
        return true;
    uint8 waitSeconds = AI_VALUE(uint8, "wait for attack time");
    return time(0) - combatStart >= static_cast<time_t>(waitSeconds);
}

bool PullAnchorDoneTrigger::IsActive()
{
    // The held DPS bots carry the same anchor marker but run their own wait
    // window ("pull hold expired"); only the puller holds the anchor for a
    // fight it is already having.
    if (ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT))
        return false;

    PositionMap& posMap = AI_VALUE(PositionMap&, "position");
    if (!posMap["pull hold"].isSet())
        return false;

    // Hold the anchor for as long as the pulled fight lasts: in combat, or
    // with a live target left to tank. Once nothing is left - the mob dead,
    // the pack finished, the hold broken off - the tank follows the party
    // again instead of standing at the anchor forever.
    if (sServerFacade.IsInCombat(bot))
        return false;

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    return !currentTarget || !currentTarget->IsAlive();
}
