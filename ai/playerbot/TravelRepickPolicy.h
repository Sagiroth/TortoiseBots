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
