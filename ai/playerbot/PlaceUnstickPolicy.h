#pragma once

#include <cmath>
#include <cstdint>

// Pure policy for the navmesh-island unstick (AiPlayerbot.DungeonRunPlaceUnstick).
//
// A teleport, summon, knockback or a placement spread can land a bot on a small
// navmesh island that has no path to the rest of the mesh (ZG walk 55: the 2nd seat
// of group 1 and the 4th seat of group 4 at the Jin'do place point, every raid).
// From there every move asks the navmesh for a route, gets PATHFIND_NOPATH, and
// MovementAction::DispatchMovement refuses the lone fallback point as
// "dispatch-short", so the bot stands still for the whole fight. The follow
// rescue's last step is the same MoveTo2 and fails the same way.
//
// The glue in MovementAction::MoveTo2 feeds every move result into a per-bot
// clock (PlaceUnstickOnMoveResult). Only a dispatch-short failure whose path type
// carries NOPATH, inside an instance, counts; a launched move resets the clock;
// any other failure neither counts nor resets. The clock restarts on a new map or
// instance, after a gap, and when the bot drifted kPlaceUnstickMoveYd from where
// the streak began. After kPlaceUnstickStreakMs the cheap gates of
// PlaceUnstickDecide run; only then one navmesh query from the bot to its master
// proves the START is cut off (PlaceUnstickStartIsolated): the core marks NOPATH
// for an off-mesh destination too, and a bot on the main mesh chasing a mob on a
// ledge is not stuck. A proven island start teleports the bot beside its master
// (combat kept, MovementAction::TryPlaceUnstick), at most once per
// kPlaceUnstickCooldownMs.

namespace ai
{
    // Mirrors MOVE_FAIL_DISPATCH_SHORT (LastMovementValue.h; a static_assert in
    // MovementActions.cpp keeps them equal) and the core PathFinder's
    // PATHFIND_NOPATH bit (TRAVEL_PATHFIND_NOPATH in TravelRepickPolicy.h).
    constexpr uint32_t kPlaceUnstickFailDispatchShort = 9;
    constexpr uint32_t kPlaceUnstickPathNoPath = 0x0008;

    // How long the NOPATH failures must last before the teleport.
    constexpr uint32_t kPlaceUnstickStreakMs = 10000;
    // A gap longer than this between two counted failures starts a new streak: a
    // bot that stopped asking to move for a while is not proven stuck by one
    // fresh failure (the island bots of walk 55 failed every ~2 s).
    constexpr uint32_t kPlaceUnstickGapMs = 5000;
    // At most one teleport per bot in this window.
    constexpr uint32_t kPlaceUnstickCooldownMs = 30000;
    // The master must be farther than this (a bot beside its master is not cut off).
    constexpr float kPlaceUnstickMasterMinYd = 20.0f;
    // The bot's own target within this range while the master fights: a fight, not a snag.
    constexpr float kPlaceUnstickFightYd = 5.0f;
    // A bot that drifted this far since its streak began is moving: the streak restarts.
    constexpr float kPlaceUnstickMoveYd = 3.0f;
    // The isolation query's farthest reachable point within this of the bot: the island signature.
    constexpr float kPlaceUnstickReachYd = 2.0f;
    // A stay or guard point within this of the island start is cleared on the teleport.
    constexpr float kPlaceUnstickAnchorYd = 5.0f;
    // The bot's pet within this of the island start goes with it.
    constexpr float kPlaceUnstickPetYd = 30.0f;

    struct PlaceUnstickState
    {
        bool streak = false;          // a NOPATH streak is running
        uint32_t streakStartMs = 0;   // WorldTimer ms of the streak's first counted failure
        uint32_t lastFailMs = 0;      // WorldTimer ms of the latest counted failure
        uint32_t mapId = 0;           // where the streak runs
        uint32_t instanceId = 0;
        float startX = 0.0f;          // the bot's spot when the streak began
        float startY = 0.0f;
        float startZ = 0.0f;
        bool teleported = false;      // a teleport was tried at lastTeleportMs
        uint32_t lastTeleportMs = 0;
        uint32_t lastStreakMs = 0;    // the streak length the last teleport fired on (log row)
    };

    // Where the bot is when a move result comes in.
    struct PlaceUnstickSpot
    {
        bool inInstance = false;
        uint32_t mapId = 0;
        uint32_t instanceId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // The one failure that feeds the clock: dispatch-short with NOPATH in the path type.
    inline bool PlaceUnstickCountsFail(uint32_t failReason, uint32_t pathType)
    {
        return failReason == kPlaceUnstickFailDispatchShort && (pathType & kPlaceUnstickPathNoPath) != 0;
    }

    inline float PlaceUnstickDist(float ax, float ay, float az, float bx, float by, float bz)
    {
        float const dx = ax - bx, dy = ay - by, dz = az - bz;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    inline void PlaceUnstickBegin(PlaceUnstickState& s, uint32_t nowMs, const PlaceUnstickSpot& at)
    {
        s.streak = true;
        s.streakStartMs = nowMs;
        s.mapId = at.mapId;
        s.instanceId = at.instanceId;
        s.startX = at.x;
        s.startY = at.y;
        s.startZ = at.z;
    }

    // One MoveTo2 result. `dispatched`: the move launched. WorldTimer ms wrap is
    // handled by unsigned subtraction.
    inline void PlaceUnstickOnMoveResult(PlaceUnstickState& s, uint32_t nowMs, bool dispatched,
        uint32_t failReason, uint32_t pathType, const PlaceUnstickSpot& at)
    {
        if (dispatched)
        {
            s.streak = false;
            return;
        }
        if (!PlaceUnstickCountsFail(failReason, pathType))
            return;
        // Only inside an instance: an open-world streak never carries into a dungeon.
        if (!at.inInstance)
        {
            s.streak = false;
            return;
        }
        if (!s.streak || nowMs - s.lastFailMs > kPlaceUnstickGapMs || s.mapId != at.mapId ||
            s.instanceId != at.instanceId ||
            !(PlaceUnstickDist(s.startX, s.startY, s.startZ, at.x, at.y, at.z) < kPlaceUnstickMoveYd))
            PlaceUnstickBegin(s, nowMs, at);
        s.lastFailMs = nowMs;
    }

    // Length of the running streak at nowMs (0 when none, or when it lapsed).
    inline uint32_t PlaceUnstickStreakMs(const PlaceUnstickState& s, uint32_t nowMs)
    {
        if (!s.streak || nowMs - s.lastFailMs > kPlaceUnstickGapMs)
            return 0;
        return nowMs - s.streakStartMs;
    }

    // The isolation query (one navmesh path from the bot to its master), pure part.
    // queryRan: the navmesh answered. startPoly / endPoly: a walkable poly under the
    // bot / the master. reachedEnd: the path ends on the master's poly (complete).
    // reachYd: its farthest reachable point from the bot. Isolated: no poly under the
    // bot at all, or a path that stops short of the master within kPlaceUnstickReachYd
    // of the bot (the walk 55 signature: "nearest reachable point is the start itself").
    // A master off the mesh proves nothing about the start.
    inline bool PlaceUnstickStartIsolated(bool queryRan, bool startPoly, bool endPoly, bool reachedEnd, float reachYd)
    {
        if (!queryRan)
            return false;
        if (!startPoly)
            return true;
        if (!endPoly)
            return false;
        if (reachedEnd)
            return false;
        return reachYd <= kPlaceUnstickReachYd;
    }

    struct PlaceUnstickInputs
    {
        bool flagOn = false;
        bool inInstance = false;        // dungeon or raid map
        bool inBattleground = false;    // battleground or arena
        bool hasMaster = false;         // a master (player or bot), not the bot, in world, same map, alive, not teleporting
        float masterYd = 0.0f;
        bool masterInSight = false;     // master within the config's sightDistance (the summon command's own bound)
        bool runCommanded = false;      // the bot's group is bound by a commander (dungeon run, raid walk)
        bool botAlive = false;
        bool botCharmed = false;
        bool botCharming = false;       // the bot holds a charm on something
        bool botCasting = false;
        bool masterFightNearBot = false; // master in combat and the bot's own live target within kPlaceUnstickFightYd of the bot
        uint32_t streakMs = 0;
        bool teleportedBefore = false;
        uint32_t sinceTeleportMs = 0;
        bool startIsolated = false;     // PlaceUnstickStartIsolated; the glue runs the query only when every other gate passed
    };

    enum class PlaceUnstickVerdict
    {
        Teleport,
        FlagOff,
        NotInstance,
        Battleground,
        NoMaster,
        MasterNear,
        MasterTooFar,
        BotDead,
        Charmed,
        Charming,
        Casting,
        Fight,
        StreakShort,
        Cooldown,
        NotIsolated,
    };

    inline PlaceUnstickVerdict PlaceUnstickDecide(const PlaceUnstickInputs& in)
    {
        if (!in.flagOn)
            return PlaceUnstickVerdict::FlagOff;
        if (in.inBattleground)
            return PlaceUnstickVerdict::Battleground;
        if (!in.inInstance)
            return PlaceUnstickVerdict::NotInstance;
        if (in.streakMs < kPlaceUnstickStreakMs)
            return PlaceUnstickVerdict::StreakShort;
        if (!in.hasMaster)
            return PlaceUnstickVerdict::NoMaster;
        if (!(in.masterYd > kPlaceUnstickMasterMinYd))
            return PlaceUnstickVerdict::MasterNear;
        // The misuse cap: beyond sight only for a commander-bound group.
        if (!in.masterInSight && !in.runCommanded)
            return PlaceUnstickVerdict::MasterTooFar;
        if (!in.botAlive)
            return PlaceUnstickVerdict::BotDead;
        if (in.botCharmed)
            return PlaceUnstickVerdict::Charmed;
        if (in.botCharming)
            return PlaceUnstickVerdict::Charming;
        if (in.botCasting)
            return PlaceUnstickVerdict::Casting;
        if (in.masterFightNearBot)
            return PlaceUnstickVerdict::Fight;
        if (in.teleportedBefore && in.sinceTeleportMs < kPlaceUnstickCooldownMs)
            return PlaceUnstickVerdict::Cooldown;
        if (!in.startIsolated)
            return PlaceUnstickVerdict::NotIsolated;
        return PlaceUnstickVerdict::Teleport;
    }

    // The query found the start connected: the streak restarts now, so the query
    // runs at most once per kPlaceUnstickStreakMs.
    inline void PlaceUnstickRestartStreak(PlaceUnstickState& s, uint32_t nowMs)
    {
        s.streakStartMs = nowMs;
    }

    // Record a teleport attempt (made or refused by the core): starts the cooldown
    // and ends the streak, so a refused teleport is retried only after a fresh
    // 10 s streak and the cooldown.
    inline void PlaceUnstickStamp(PlaceUnstickState& s, uint32_t nowMs)
    {
        s.lastStreakMs = PlaceUnstickStreakMs(s, nowMs);
        s.teleported = true;
        s.lastTeleportMs = nowMs;
        s.streak = false;
    }

    // The fight test (pure part): master in combat, bot's target alive and within kPlaceUnstickFightYd.
    inline bool PlaceUnstickIsFight(bool masterInCombat, bool hasLiveTarget, float targetYd)
    {
        return masterInCombat && hasLiveTarget && targetYd <= kPlaceUnstickFightYd;
    }

    // A stay or guard point the teleport clears: set, on the island's map, within kPlaceUnstickAnchorYd of its start.
    inline bool PlaceUnstickClearsAnchor(bool anchorSet, bool sameMap, float anchorFromStartYd)
    {
        return anchorSet && sameMap && anchorFromStartYd <= kPlaceUnstickAnchorYd;
    }

    // The bot's pet goes with it: alive, in world, within kPlaceUnstickPetYd of the island start.
    inline bool PlaceUnstickMovesPet(bool petLive, float petFromStartYd)
    {
        return petLive && petFromStartYd <= kPlaceUnstickPetYd;
    }
}
