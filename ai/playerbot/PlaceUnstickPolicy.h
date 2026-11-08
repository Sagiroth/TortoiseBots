#pragma once

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
// carries NOPATH counts; a launched move resets the clock; any other failure
// neither counts nor resets. After kPlaceUnstickStreakMs of such failures the
// decision below (PlaceUnstickDecide) teleports the bot to its master with the
// summon action's teleport (SummonAction::Teleport), at most once per
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

    struct PlaceUnstickState
    {
        bool streak = false;          // a NOPATH streak is running
        uint32_t streakStartMs = 0;   // WorldTimer ms of the streak's first counted failure
        uint32_t lastFailMs = 0;      // WorldTimer ms of the latest counted failure
        bool teleported = false;      // a teleport was tried at lastTeleportMs
        uint32_t lastTeleportMs = 0;
        uint32_t lastStreakMs = 0;    // the streak length the last teleport fired on (log row)
    };

    // The one failure that feeds the clock: dispatch-short with NOPATH in the path type.
    inline bool PlaceUnstickCountsFail(uint32_t failReason, uint32_t pathType)
    {
        return failReason == kPlaceUnstickFailDispatchShort && (pathType & kPlaceUnstickPathNoPath) != 0;
    }

    // One MoveTo2 result. `dispatched`: the move launched. WorldTimer ms wrap is
    // handled by unsigned subtraction.
    inline void PlaceUnstickOnMoveResult(PlaceUnstickState& s, uint32_t nowMs, bool dispatched,
        uint32_t failReason, uint32_t pathType)
    {
        if (dispatched)
        {
            s.streak = false;
            return;
        }
        if (!PlaceUnstickCountsFail(failReason, pathType))
            return;
        if (!s.streak || nowMs - s.lastFailMs > kPlaceUnstickGapMs)
        {
            s.streak = true;
            s.streakStartMs = nowMs;
        }
        s.lastFailMs = nowMs;
    }

    // Length of the running streak at nowMs (0 when none, or when it lapsed).
    inline uint32_t PlaceUnstickStreakMs(const PlaceUnstickState& s, uint32_t nowMs)
    {
        if (!s.streak || nowMs - s.lastFailMs > kPlaceUnstickGapMs)
            return 0;
        return nowMs - s.streakStartMs;
    }

    struct PlaceUnstickInputs
    {
        bool flagOn = false;
        bool inInstance = false;        // dungeon or raid map
        bool inBattleground = false;    // battleground or arena
        bool hasMaster = false;         // a master (player or bot), not the bot, in world, same map, alive, not teleporting
        float masterYd = 0.0f;
        bool botAlive = false;
        bool botCharmed = false;
        bool botCasting = false;
        bool masterFightNearBot = false; // master in combat and the bot's own live target within kPlaceUnstickFightYd of the bot
        uint32_t streakMs = 0;
        bool teleportedBefore = false;
        uint32_t sinceTeleportMs = 0;
    };

    enum class PlaceUnstickVerdict
    {
        Teleport,
        FlagOff,
        NotInstance,
        Battleground,
        NoMaster,
        MasterNear,
        BotDead,
        Charmed,
        Casting,
        Fight,
        StreakShort,
        Cooldown,
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
        if (!in.botAlive)
            return PlaceUnstickVerdict::BotDead;
        if (in.botCharmed)
            return PlaceUnstickVerdict::Charmed;
        if (in.botCasting)
            return PlaceUnstickVerdict::Casting;
        if (in.masterFightNearBot)
            return PlaceUnstickVerdict::Fight;
        if (in.teleportedBefore && in.sinceTeleportMs < kPlaceUnstickCooldownMs)
            return PlaceUnstickVerdict::Cooldown;
        return PlaceUnstickVerdict::Teleport;
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
}
