
#include "playerbot/playerbot.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "PullTriggers.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/actions/PullActions.h"

using namespace ai;

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

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
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

    const time_t secondsSincePullStarted = time(0) - strategy->GetPullStartTime();
    const bool pullback = ai->HasStrategy("pull back", BotState::BOT_STATE_COMBAT);

    if (pullback && strategy->HasPullActionCompleted())
    {
        PositionMap& posMap = AI_VALUE(PositionMap&, "position");
        PositionEntry pullPosition = posMap["pull"];
        if (!pullPosition.isSet() || pullPosition.mapId != bot->GetMapId())
            return true;

        // Do not discard the return anchor just because the target died,
        // changed victim, or the normal pull timeout elapsed while returning.
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
