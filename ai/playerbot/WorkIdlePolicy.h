#pragma once

#include <cstdint>
#include <ctime>

namespace ai
{
    // Empty-destination WORK release for masterless pool bots (workidle).
    //
    // A masterless pool bot that arrives at its travel destination flips the
    // target to WORK, and WORK counts as "travel target active", which blocks
    // the next request (RequestTravelTargetAction) and the idle drift
    // (IdleWanderAction). When the destination has nothing to do - a grind
    // spot with no attackable prey, a quest giver with nothing to take or
    // hand in, a gather node gone, an RPG spot already chatted out - the
    // target's own IsActive verdict stays green (it reads static data, not
    // the live scene), so the bot stands until WORK expires (~5 min default,
    // 1 min for RPG/quest-objective/explore trips).
    //
    // Donor mod-playerbots NewRpg never holds a status with nothing to do:
    // GO_GRIND returns to WANDER_RANDOM on arrival and WANDER_NPC returns to
    // IDLE when no NPC can be found (NewRpgAction.cpp), and the quest pursuit
    // abandons the POI after 5 min with no objective progress instead of
    // holding it.
    //
    // Pure decision rule, no core includes: the caller (TravelTarget, which
    // owns the status) passes the already-cached scene reads, and the rule
    // only says whether the hold has gone stale.

    // How long a WORK hold may sit with nothing to do before it releases:
    // three pool visits at the ~10 s cadence, so one empty read cannot drop
    // a trip whose prey is still spawning or whose corpse is still looted.
    constexpr std::int64_t kWorkIdleReleaseSec = 30;

    // Manual-time anchor key (per-bot value, zero cost when unset).
    inline char const* WorkIdleAnchorKey() { return "work idle since"; }

    // Release verdict: the WORK stay started at anchorTime, it is now `now`,
    // and neither signal shows anything to do - no attackable grind prey in
    // the (already cached) pick, and nobody fighting the bot. Both signals
    // are caller reads of values the engine already maintains on their own
    // cadence ("grind target" 2 s, "attackers" 2 s): no DB hit, no world
    // scan, no graph rebuild at this call site.
    inline bool WorkIdleStale(std::int64_t anchorTime, std::int64_t now,
        bool hasGrindPrey, bool hasAttackers,
        std::int64_t horizonSec = kWorkIdleReleaseSec)
    {
        if (anchorTime == 0)
            return false;
        if (now - anchorTime < horizonSec)
            return false;
        return !hasGrindPrey && !hasAttackers;
    }

    // Anchor upkeep for the caller: start the clock on the first WORK tick
    // with nothing to do, keep it while the stay is unproductive, clear it
    // the moment either signal shows work. Progress (a prey, an attacker,
    // a kill, loot) re-arms from zero instead of releasing mid-fight.
    inline std::int64_t WorkIdleAnchor(std::int64_t anchorTime, std::int64_t now,
        bool hasGrindPrey, bool hasAttackers)
    {
        if (hasGrindPrey || hasAttackers)
            return 0;
        return anchorTime ? anchorTime : now;
    }
}
