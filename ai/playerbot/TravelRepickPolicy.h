#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

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

    // Same-quest giver re-pick loop (night2 pool: 62 bots picked 3+ givers in
    // 15 min, same quest id re-picked standing at the NPC with no accept,
    // move-fail or drop between picks - each CopyTarget restarts the 5-min
    // WORK clock, so the hold never expires). The 3rd consecutive pick of
    // one quest id parks the quest errand 1 min (the standard empty-search
    // park), bounding the loop without breaking legit retries: a different
    // quest, a taker, or any other purpose resets the streak, and the park
    // only delays the 4th re-pick - the pick itself still lands. Givers
    // only: takers settle through their own hand-in path.
    constexpr int GIVER_REPICK_PARK_AFTER = 3;
    inline bool GiverRepickParksQuest(int consecutiveSameQuestPicks)
    {
        return consecutiveSameQuestPicks >= GIVER_REPICK_PARK_AFTER;
    }

    // Held-prey travel veto, starved-only gate (night2 heldprey2): the
    // non-combat queue is one relevance race and every travel request
    // (6.3-6.99) outranks "attack anything" (5.0), so a bot holding a
    // usable pick with no journey spends every visit re-requesting travel
    // while any purpose stays unparked - each search then refuses
    // (empty/rejected) and parks a minute, and with ~8 purposes rotating
    // the attack never wins. Vetoing on the held pick alone would also pin
    // a bot that just finished a trip wherever mobs stand (no quests,
    // trainers, vendors - organic levelling dead), so the veto only
    // engages once the bot's own recent searches prove starved: a
    // successful pick sets no park, while every failed search does
    // (empty/invalid 1 min, quest-empty 10 min, move-fail drop and stuck
    // retire 5 min, giver re-pick guard and fruitless trainer visit their
    // own windows - all filed under "no travel purpose until::<key>", the
    // same timestamp the request gate reads). Three parked purposes out
    // of the twelve below means the rotation is failing broadly, not one
    // errand waiting out a moment: one or two parks is an ordinary
    // questing bot between trips. Counted live at the call site from the
    // existing timestamps - no new value, no scan. The caller only counts
    // while a pick is held and no journey is active, and only for
    // masterless pool bots (owned/hired journeys are player-ordered).
    constexpr int TRAVEL_STARVED_PARKED_PURPOSES = 3;

    // Park keys the starved count reads: the quest errand ("quest", its
    // requests carry no qualifier) plus every numeric travel purpose in
    // the request tables (Grind 4096, GenericRpg 64, Explore 262144,
    // GatherMining 32768, GatherHerbalism 65536, GatherFishing 131072,
    // Boss 8192, Vendor 512, Repair 256, AH 1024, Mail 2048). Full
    // "manual time" keys, so the call site pays no per-tick
    // concatenation. Named errands (trainer class, city, ...) are out:
    // their parks are common on healthy bots (a trainer with nothing
    // affordable parks ten minutes), and counting them would veto
    // questing bots that are succeeding everywhere else.
    inline std::vector<std::string> const& TravelStarvedParkKeys()
    {
        static std::vector<std::string> const keys =
        {
            "no travel purpose until::quest",
            "no travel purpose until::4096",
            "no travel purpose until::64",
            "no travel purpose until::262144",
            "no travel purpose until::32768",
            "no travel purpose until::65536",
            "no travel purpose until::131072",
            "no travel purpose until::8192",
            "no travel purpose until::512",
            "no travel purpose until::256",
            "no travel purpose until::1024",
            "no travel purpose until::2048"
        };
        return keys;
    }

    // Starved verdict over the caller's fresh-park count: three or more
    // parked purposes means recent searches came back empty across the
    // rotation, so holding the pick outranks the next errand until the
    // pick resolves (kill, tap, or cache refresh clears it and the veto
    // lifts with it).
    inline bool TravelSearchesStarved(int parkedPurposeCount)
    {
        return parkedPurposeCount >= TRAVEL_STARVED_PARKED_PURPOSES;
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
    // A grind destination whose walk keeps failing is given up like a wedged
    // combat target (ReachTargetActions.h): the creature kind goes on the
    // same "unreachable entries" blacklist, so the grind gate
    // (GrindTravelDestination::IsActive) stops offering it for a while and
    // the next pick walks a different kind instead of re-picking the same
    // spot. Live 2026-10-08 pool: 103 of 119 grind move-failures probed
    // NOPATH from a median 2328 yd away, yet the same kind was re-picked
    // within a minute because nothing recorded the failure - the bot
    // dropped, re-picked, failed and fired "move stuck" resets in place
    // for 10+ min (frozen grind bots 107 -> 276 while the empty-search
    // fallthrough re-armed local grind). Grind only: quest takers settle
    // through their own hand-in path and services through their teleport
    // rescue, and only a same-map probe that ran on the navmesh and found
    // no path counts - cross-map (never probed) and unloaded tiles
    // (not-using-path) keep the target, and entry-less destinations have
    // no kind to blacklist. Masterless pool bots only: an owned bot's
    // player may be walking it there themselves.
    inline bool TravelMoveFailBlacklistsKind(bool masterlessRandom, bool grindDestination,
        bool meshProbedNoPath, int32_t entry)
    {
        return masterlessRandom && grindDestination && meshProbedNoPath && entry > 0;
    }

    // A trip that never arrived cools down without ever parking its purpose:
    // the kind blacklist (above) only covers the one failed creature kind,
    // and the 6-fail drop below never fires - the 60 s cooldown expires first
    // and the expiry re-requests immediately. Live night2 pool: 1 grind drop
    // vs 45 grind move-fails in 8 min, with capital-loop bots (Stormwind ->
    // Westfall) re-picking the same zone every ~2.5 min from the same
    // standstill. A never-arrived cooldown therefore parks its purpose like
    // a drop (5 min, same keys the request gate reads): other purposes keep
    // working, and the same destination cannot be re-picked at once. An
    // arrived trip (WORK) keeps today's behaviour - its spot emptied, so a
    // fresh nearby pick is desired. Masterless pool bots only; owned bots
    // keep player control. The caller passes the status the trip held when
    // it cooled down.
    constexpr time_t TRAVEL_COOLDOWN_PARK_SECONDS = 5 * 60;
    inline bool TravelCooldownParksPurpose(bool masterlessRandom, bool wasTraveling)
    {
        return masterlessRandom && wasTraveling;
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
