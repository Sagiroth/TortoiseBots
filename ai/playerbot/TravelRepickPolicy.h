#pragma once

#include <cstdint>
#include <ctime>
#include <string>

// Pure policy for the travel re-pick churn fixes (loops handover sections 3
// and 4): combat-stuck resets kept no travel target while move-stuck
// saved/restored it, so bots re-picked a quest target every 4-6 s after a
// combat-stuck UnstuckTrip; a successful refresh reported failure (tripping
// ACTION_LOOP telemetry while the destination stayed active); an
// invalid/empty async destination result set no park (re-request at tick
// speed); and the TravelTarget "(from null)" line could never fire as coded
// (old target read after CopyTarget), while the live binary logged the old
// null zone. The world-facing parts (AI values, travel target, logEvent)
// live in UnstuckAction and ChooseTravelTargetAction; the decisions below are
// pure so they can be tested on their own.

namespace ai
{
    // How long an invalid async destination result parks its purpose: the
    // same 1-minute purpose park the empty-search path uses, so a bot does
    // not re-request (and re-search) on the very next tick. Same keys as the
    // empty-search path ("no active travel destinations" flag + "no travel
    // purpose until::<purpose>" timestamp, which the request gate reads).
    constexpr time_t TRAVEL_FUTURE_INVALID_PARK_SECONDS = 60;

    // Purpose key the invalid-result park is filed under: the empty-search
    // path files an empty purpose as "quest", and the request gate reads the
    // same key back ("quest" for the quest errand, the numeric purpose
    // otherwise).
    inline std::string TravelInvalidParkKey(const std::string& futurePurpose)
    {
        return futurePurpose.empty() ? "quest" : futurePurpose;
    }

    // Park key for a dropped/retired purpose: the request gate files the quest
    // errand under "quest" (its request carries no qualifier) and every other
    // purpose under its qualifier string, so a drop parks exactly the key the
    // gate reads back. Empty (a purpose wiped by a reset) means the quest
    // errand, like the invalid-result park above.
    inline std::string TravelPurposeParkKey(const std::string& futurePurpose)
    {
        return futurePurpose.empty() ? "quest" : futurePurpose;
    }
    // Empty-search fallthrough (2026-10-08): a masterless pool bot whose
    // choose step ends with no target (search empty or every candidate
    // refused) used to park the purpose and stand until the next pool visit
    // (~26 s) re-ranked purposes. The donor (mod-playerbots
    // ChooseTravelTargetAction::getNewTarget) never parks: a purpose with
    // nothing falls through to the next one, ending at grind, in the same
    // decision. The small port: when the choose step ends empty for a
    // non-grind purpose, re-arm a local grind search (the same window the
    // grind purpose uses) before parking, so the bot leaves the decision
    // with a re-armed search instead of a bare park. Owned/hired bots skip
    // (their player decides); the grind purpose itself skips (nothing to
    // fall through to); a parked or out-of-window grind skips too (its own
    // search just failed, or camp owns this slice of the hour). Pure inputs
    // only: the caller reads the cached park timestamp and phase, launches
    // std::async like RequestTravelTargetAction::Execute, and the follow-up
    // pass (BotManager pass 2b) picks it up next tick via PREPARE.
    inline bool TravelEmptyFallthroughToGrind(bool masterlessRandom,
        const std::string& emptyPurposeKey, const std::string& grindPurposeKey,
        bool grindWindowOpen)
    {
        if (!masterlessRandom || !grindWindowOpen)
            return false;
        if (emptyPurposeKey.empty() || emptyPurposeKey == grindPurposeKey)
            return false;
        return true;
    }


    // PathFinder::getPathType() bucket for the TravelMoveFailed row: NOPATH
    // vs INCOMPLETE vs a tile that never used the navmesh could not be
    // separated from the CSV (finding 14). Bit values mirror the core
    // PathFinder::PathType enum (tortoise-wow src/game/Maps/PathFinder.h);
    // the caller passes the raw type, the tag names the bucket. A cross-map
    // probe never runs (the caller tags those without querying).
    constexpr uint32_t TRAVEL_PATHFIND_INCOMPLETE = 0x0004;
    constexpr uint32_t TRAVEL_PATHFIND_NOPATH = 0x0008;
    constexpr uint32_t TRAVEL_PATHFIND_NOT_USING_PATH = 0x0010;
    inline std::string TravelMoveFailPathTag(uint32_t pathType)
    {
        if (pathType & TRAVEL_PATHFIND_NOT_USING_PATH)
            return "not-using-path";
        if (pathType & TRAVEL_PATHFIND_NOPATH)
            return "nopath";
        if (pathType & TRAVEL_PATHFIND_INCOMPLETE)
            return "incomplete";
        return "complete";
    }

    // A bot that cannot path anywhere from where it stands (live 2026-10-05:
    // Orgrimmar auction house, every target from a 35 yd hop to a far zone
    // came back NOPATH for 12-19 min until the 15 min long-stuck rescue
    // fired). Each target that fails NOPATH from within 10 yd of the streak's
    // first failure extends the streak; three of them inside 20 min mean the
    // spot, not the targets, is the problem, and the long-stuck rescue
    // relocates the bot. A drop that is not NOPATH leaves the streak alone, a
    // failure elsewhere or after the window starts a new one.
    // The nearness check runs against the first failure's position, not the
    // last one: after a hearth, repop or taxi moves the bot, the next NOPATH
    // failure lands far from the stale anchor and restarts the streak at the
    // new spot (the caller re-anchors whenever the streak returns to 1).
    // Stormwind upper floors (2026-10-08): the anchor spans one floor only.
    // A bot that climbs or drops a level keeps failing NOPATH from the same
    // x/y but a new z, so a same-x/y failure more than NOPATH_TRAP_FLOOR_Z_YD
    // above or below the anchor starts a fresh streak instead of joining it.
    constexpr int NOPATH_TRAP_TARGETS = 3;
    constexpr float NOPATH_TRAP_RADIUS_YD = 10.0f;
    constexpr float NOPATH_TRAP_FLOOR_Z_YD = 10.0f;
    constexpr time_t NOPATH_TRAP_WINDOW_SECONDS = 20 * 60;

    inline int NoPathTrapStreak(int streak, bool noPath, bool nearAnchor, bool windowExpired, bool sameFloor = true)
    {
        if (!noPath)
            return streak;
        if (streak > 0 && nearAnchor && !windowExpired && sameFloor)
            return streak + 1;
        return 1;
    }

    inline bool IsNoPathTrapped(int streak)
    {
        return streak >= NOPATH_TRAP_TARGETS;
    }

    // Single-point dispatch gate (review #475): ResolveMovePath tags its NOPATH
    // fallback as a lone entry-less portal point, so DispatchMovement can tell
    // "no route at all" from a clipped real path. Walking the fallback would
    // DecRetry forever and the unreachable target would never drop; only a
    // real clipped path earns the single-point MovePoint.
    // PathNodeType lives in TravelNode.h (module C++, not policy-testable), so
    // the caller passes the already-read (pointCount, type, entry) triple.
    // Type 7 / entry 0 is NODE_STATIC_PORTAL without an entry: real route
    // portals and teleports always carry one.
    inline bool TravelIsNoRouteFallbackPoint(size_t pointCount, int nodeType, unsigned entry)
    {
        constexpr int NO_ROUTE_FALLBACK_TYPE = 7;
        return pointCount == 1 && nodeType == NO_ROUTE_FALLBACK_TYPE && entry == 0;
    }
    // May a stuck reset keep its travel target - the same keep rule the
    // move-stuck path applies: an active target with a real destination and
    // position survives PlayerbotAI::Reset(true); anything else (no target,
    // idle, null destination) resets bare.
    inline bool ShouldKeepTravelAcrossStuckReset(bool hasTarget, bool targetActive,
        bool hasDestination, bool hasPosition, bool isNullDestination)
    {
        return hasTarget && targetActive && hasDestination && hasPosition && !isNullDestination;
    }

    // Bounded keep: after 3 consecutive stuck keeps without real progress
    // (30 yd from where the keep streak started) the target is retired
    // instead of preserved. Distance arrives squared, like the caller's
    // sqDistance comparison; exactly 30 yd still counts as progress.
    inline bool ShouldRetireStuckTravelKeep(int keepCount, float distSqFromAnchorYd)
    {
        constexpr float RETIRE_RADIUS_YD = 30.0f;
        return keepCount >= 3 && distSqFromAnchorYd < RETIRE_RADIUS_YD * RETIRE_RADIUS_YD;
    }

    // A target counts as null when it holds no destination or the null
    // destination. Callers must snapshot the OLD target BEFORE CopyTarget
    // overwrites it: read after, the old destination is already the new one,
    // so a pick from a null target could never be recognized and the "(from
    // null)" marker never fired. The same predicate reads the NEW target for
    // the reset skip below.
    inline bool TravelTargetIsNull(bool hasDestination, bool isNullDestination)
    {
        return !hasDestination || isNullDestination;
    }

    // Resets (ResetTargetAction) pass a fresh null target through
    // setNewTarget: the NEW side is null, whatever the stale purpose value
    // says (it is left over from the last request). Skipped, never logged as
    // a pick. The second clause preserves the earlier skip (old null with
    // purpose None); normal picks are real on the new side, so nothing
    // legitimate stops logging.
    inline bool TravelIsResetToNull(bool pickedFromNull, bool newIsNull, const std::string& purposeName)
    {
        return newIsNull || (pickedFromNull && purposeName == "None");
    }
}
