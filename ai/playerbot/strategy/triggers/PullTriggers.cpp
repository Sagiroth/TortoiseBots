
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "PullTriggers.h"
#include "playerbot/strategy/actions/TwinsOpeningActions.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/actions/PullActions.h"
#include "../../policy/class/ShamanTankPolicy.h"

using namespace ai;

bool PullStartTrigger::IsActive()
{
    const PullStrategy* strategy = PullStrategy::Get(ai);
    return strategy && strategy->IsPullPendingToStart();
}

bool PullContinueTrigger::IsActive()
{
    PullStrategy* strategy = PullStrategy::Get(ai);
    if (!strategy || !strategy->HasPullStarted() || strategy->IsPullPendingToStart() ||
        strategy->HasPullActionCompleted() || !bot->IsAlive())
        return false;
    Unit* target = strategy->GetTarget();
    if (!target || !target->IsInWorld() || !target->IsAlive() ||
        time(nullptr) - strategy->GetPullStartTime() >= strategy->GetMaxPullTime())
        return false;
    strategy->TraceCommand("continue-trigger");
    return true;
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

    // Shaman tanks fight on mana (shocks, Chain Lightning, totems): do not
    // pull below 80% own mana, drink first (owner decision, issue #1125).
    // Other tanks keep today's behaviour — their kits cost rage, not mana.
    if (bot->GetClass() == CLASS_SHAMAN && PlayerbotAI::IsTank(bot))
    {
        bool const hasMana = bot->GetMaxPower(POWER_MANA) > 0;
        uint8 const manaPct = hasMana ? (100 * bot->GetPower(POWER_MANA)) / bot->GetMaxPower(POWER_MANA) : 100;
        if (!TankShamanPullManaReady(hasMana, manaPct))
            return false;
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
        ReleaseHeldPartyNow(bot);
        return true;
    }

    // Linked proximity aggro is not the paired ranged opening. Keep its
    // authorization until both casts are dispatched or the coordinator cancels.
    if(TwinsPairedPullPending(ai)) return false;

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
            ReleaseHeldPartyNow(bot);
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

    // The approach window slides while the tank closes in (PullReachPolicy.h):
    // a progressing cross-room walk keeps its target past the flat window
    // (issue #910). A wedged approach still ends exactly like before, and the
    // total cap bounds even a shuffling one.
    if (strategy->HasPullApproachExpired())
        return true;

    // The mob came to the puller before the pull action ran (a corner pull:
    // the tank walks to a mob it cannot see and the mob meets it on the way).
    // It is on the tank in melee, so the pull has landed; ending it here hands
    // the mob to the rotation (PullEndAction). Waiting for the pull action
    // instead kept PullMultiplier muting every action below ACTION_INTERRUPT
    // until the time cap above: in Uldaman the tank stood 8 s with its back to
    // the mob, no swing, no ability, no turn. A pullback must still walk
    // home: landing it here would end the pull at the meeting point and skip
    // the return leg entirely (the tank fights where they met instead of
    // pulling back to the party), so the meet completes the pull action and
    // the return leg above ends the pull on arrival.
    if (!strategy->HasPullActionCompleted() && target->GetVictim() == bot &&
        bot->CanReachWithMeleeAutoAttack(target))
    {
        if (pullback)
        {
            // Non-const strategy: meeting the mob is the landing.
            const_cast<PullStrategy*>(strategy)->OnPullActionCompleted();
            return false;
        }
        return true;
    }

    // An intercepted pullback is a landed pull too: pack members reaching the
    // tank before its own shot's target does mean the fight has started. The
    // meet branch above only sees the shot's target; without this the return
    // leg (and its bounded clock) never arms, and the tank stays pinned at the
    // camp for the approach cap. Landing here hands the fight over through the
    // return leg like any other pullback - the cap bounds it even under fire.
    if (pullback && !strategy->HasPullActionCompleted() && !bot->GetAttackers().empty())
    {
        const_cast<PullStrategy*>(strategy)->OnPullActionCompleted();
        return false;
    }

    // An ordinary pull hands control back to combat as soon as its pull action
    // succeeds.  Before that, retain the request while the tank closes to its
    // class-specific melee/ranged pull distance.
    return strategy->HasPullActionCompleted();
}

bool PullHoldExpiredTrigger::IsActive()
{
    // Keyed on the window's own clock, not on the anchor marker or the combat
    // clock: combat end, death and movement orders drop the marker and zero
    // the combat clock, and a window read through them was never ended.
    if (!ai->HasPullWindow())
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    if (!context)
        return false;
    return ai->PullWindowExpired(AI_VALUE(uint8, "wait for attack time"));
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

    // Hold the anchor only while the pulled mob is still on its way. Once it
    // has arrived (in melee), stopped short (a caster), or died, the anchor
    // has done its job: a stay held for the whole fight kept the tank from
    // reaching the rest of the pack, so it stood idle while the other mobs
    // beat on the party. mod-playerbots' pull back holds no stay at all.
    //
    // This runs inside the combat engine, which is evaluated between reaction
    // ticks, while PlayerbotAI::OnCombatEnded is the guarantee (it fires with
    // the engine switch, where this trigger no longer exists). Both call the
    // same release.
    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (!currentTarget || !currentTarget->IsAlive())
        return true;

    // The pulled mob left the fight entirely (it reset and went home): nothing
    // is coming to the anchor, and a wander at home must not keep the tank
    // parked on it. The bot's own armed attack cannot be the test here - it
    // stays on the reset mob for good, while the mob itself is out of combat
    // and never comes back.
    if (!currentTarget->IsInCombat())
        return true;

    return !sServerFacade.isMoving(currentTarget) || bot->CanReachWithMeleeAutoAttack(currentTarget);
}
