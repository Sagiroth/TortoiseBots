#pragma once

// When has a bot reached its stay point? One rule for the three places that ask:
// the stay walk's isUseful (ReturnToStayPositionAction, PositionAction.cpp), its
// trigger (ReturnToStayPositionTrigger::IsActive, GenericTriggers.cpp) and the run
// leader's stalled-walk rule in DungeonRunPolicy.h (its follow deadband was this rule until
// round 1 of the review found it added nothing to the stalled arrival, and it was removed).
//
// Crescent Grove r26 step 272 (2026-10-23, MOVE-USELESS): a tauren lead bot
// (bounding radius about 0.97 yd) stood 5.67 yd from its point on the ground and
// 6.6 yd below it (8.7 yd in 3D). The walk's isUseful measured the ground distance
// minus the bounding radius, rounded to 0.1 (4.7 yd, inside the 5.0 yd follow
// range: USELESS, so every `cmd move` was refused), the trigger measured 3D minus
// the radius (7.7 yd: active every tick) and the run leader's deadband measured the
// raw ground distance (5.67 yd: not arrived). The three disagreed and the run
// paused four times. The rule here is the raw 3D distance: no bounding radius (so
// it does not depend on the bot's race) and no rounding.
// Pure decision logic, unit-tested in tools/test_stay_arrival_policy.cpp; the call
// sites are pinned by tools/test_stay_arrival_wiring.py.

#include <cmath>
#include <cstdint>

namespace TortoiseBots
{

// dx, dy, dz: the bot's position minus the stay point. range: the follow range
// (PlayerbotAI::GetRange("follow"); AiPlayerbot.RaidFollowDistance, 4.0 yd, inside
// a dungeon). True when the bot is within range of the point in 3D.
inline float StayPointDistance(float dx, float dy, float dz)
{
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline bool StayPointReached(float dx, float dy, float dz, float range)
{
    return StayPointDistance(dx, dy, dz) <= range;
}

// The stay walk's back-off (the Codex review of 442a2bdf): a stay point the bot cannot
// reach (a point above it with no path up) failed its walk every engine tick for as long
// as the bot stayed. After kStayWalkFailsBeforeBackoff failed walks in a row the walk
// rests for kStayWalkBackoffFirstMs, each later rest twice as long up to
// kStayWalkBackoffMaxMs; a failure right after a rest rests again.
//
// A bot on a transport (a boat, a zeppelin, an elevator) is not judged and does not walk:
// its world position moves with the transport (round 3: a passenger accrued failures).
//
// A walk is judged by where it takes the bot, not by its dispatch (round 2 of the review:
// MoveTo returns true for a path that makeShortCut emptied, a wait for a transport or a
// one-point MovePoint that moves nothing). A dispatch starts an attempt; the attempt
// fails when the bot stood still for kStayAttemptGraceMs on end (from the dispatch or the
// last tick it was seen moving; round 3: a pause in a walk under way is not a stop) or
// gained less than kStayAttemptStepYd in 3D for kStayAttemptStallMs while short of the
// point, and succeeds when the bot reached the point or came kStayAttemptGainYd nearer
// than at the dispatch. A refused dispatch fails at once. Failures in combat do not
// count. A success, reaching the point, a new stay point or an explicit order (cmd move,
// come, hold, go, stay: StayWalkOrdered) starts it all over.
constexpr std::uint32_t kStayWalkFailsBeforeBackoff = 3;
constexpr std::uint32_t kStayWalkBackoffFirstMs = 10000;
constexpr std::uint32_t kStayWalkBackoffMaxMs = 60000;
constexpr std::uint32_t kStayAttemptGraceMs = 1000;
constexpr std::uint32_t kStayAttemptStallMs = 3000;
constexpr float kStayAttemptStepYd = 1.0f;
constexpr float kStayAttemptGainYd = 3.0f;

struct StayWalkBackoff
{
    std::uint32_t fails = 0;     // failed walks in a row (out of combat)
    std::uint32_t lengthMs = 0;  // the last rest's length (0: none yet)
    std::uint32_t untilMs = 0;   // WorldTimer ms the rest ends (0: none)
    bool keyed = false;          // the stay point below is the one the counts belong to
    float x = 0.0f, y = 0.0f, z = 0.0f;
    std::uint32_t mapId = 0;
    bool attemptOn = false;          // a dispatched walk is being judged
    std::uint32_t attemptStartMs = 0;
    float attemptStartYd = 0.0f;     // 3D distance to the point at the dispatch
    float attemptBestYd = 0.0f;      // nearest since the last kStayAttemptStepYd of progress
    std::uint32_t attemptProgressMs = 0;
    bool stoppedSeen = false;        // the bot stands still since stoppedSinceMs (no move seen since)
    std::uint32_t stoppedSinceMs = 0;
};

// The stay point of the counts: a different point starts them over.
inline void StayWalkTrack(StayWalkBackoff& b, float x, float y, float z, std::uint32_t mapId)
{
    if (b.keyed && b.x == x && b.y == y && b.z == z && b.mapId == mapId)
        return;
    b = StayWalkBackoff();
    b.keyed = true;
    b.x = x;
    b.y = y;
    b.z = z;
    b.mapId = mapId;
}

// An explicit order set the stay point (even to the same spot): a fresh walk.
inline void StayWalkOrdered(StayWalkBackoff& b)
{
    b = StayWalkBackoff();
}

// The walk works (the bot reached the point or came clearly nearer): nothing to back off from.
inline void StayWalkSucceeded(StayWalkBackoff& b)
{
    b.fails = 0;
    b.lengthMs = 0;
    b.untilMs = 0;
    b.attemptOn = false;
}

// A walk failed. Out of combat it counts toward the rest; in combat (a path clipped at a
// hostile in reach, a stun) it does not.
inline void StayWalkFailed(StayWalkBackoff& b, bool inCombat, std::uint32_t nowMs)
{
    b.attemptOn = false;
    if (inCombat)
        return;
    if (++b.fails < kStayWalkFailsBeforeBackoff)
        return;
    std::uint32_t const doubled = b.lengthMs ? b.lengthMs * 2 : kStayWalkBackoffFirstMs;
    b.lengthMs = doubled < kStayWalkBackoffMaxMs ? doubled : kStayWalkBackoffMaxMs;
    b.untilMs = (nowMs + b.lengthMs) | 1u; // the low bit keeps an end at ms 0 apart from "none"
}

// The walk's dispatch (ReturnToStayPositionAction::Execute): a refused one failed; an
// accepted one starts an attempt unless one is under way (the action re-sends the walk
// every tick it runs).
inline void StayWalkDispatched(StayWalkBackoff& b, bool moved, float distYd, bool inCombat, bool onTransport,
    std::uint32_t nowMs)
{
    if (onTransport)
        return;
    if (!moved)
    {
        StayWalkFailed(b, inCombat, nowMs);
        return;
    }
    if (b.attemptOn)
        return;
    b.attemptOn = true;
    b.attemptStartMs = nowMs;
    b.attemptStartYd = distYd;
    b.attemptBestYd = distYd;
    b.attemptProgressMs = nowMs;
    b.stoppedSeen = true; // until the bot is seen moving, the stop runs from the dispatch
    b.stoppedSinceMs = nowMs;
}

// The walk rests now. Wraps like the core's getMSTimeDiff.
inline bool StayWalkBackingOff(StayWalkBackoff const& b, std::uint32_t nowMs)
{
    return b.untilMs && static_cast<std::int32_t>(b.untilMs - nowMs) > 0;
}

// The stay walk is due (the walk's isUseful and its trigger, every tick): judges the
// attempt under way, then answers whether the bot is short of the point and the walk
// does not rest. reached / distYd: StayPointReached / StayPointDistance to the point;
// moving: the bot has a move under way.
inline bool StayWalkDue(StayWalkBackoff& b, bool reached, float distYd, bool moving, bool inCombat, bool onTransport,
    std::uint32_t nowMs)
{
    if (onTransport)
    {
        b.attemptOn = false; // nothing judged, nothing counted
        return false;
    }
    if (reached)
    {
        StayWalkSucceeded(b);
        return false;
    }
    if (b.attemptOn)
    {
        if (distYd <= b.attemptStartYd - kStayAttemptGainYd)
            StayWalkSucceeded(b);
        else
        {
            if (distYd < b.attemptBestYd - kStayAttemptStepYd)
            {
                b.attemptBestYd = distYd;
                b.attemptProgressMs = nowMs;
            }
            if (moving)
                b.stoppedSeen = false;
            else if (!b.stoppedSeen)
            {
                b.stoppedSeen = true;
                b.stoppedSinceMs = nowMs;
            }
            bool const stopped = b.stoppedSeen && static_cast<std::uint32_t>(nowMs - b.stoppedSinceMs) >= kStayAttemptGraceMs;
            bool const stalled = static_cast<std::uint32_t>(nowMs - b.attemptProgressMs) >= kStayAttemptStallMs;
            if (stopped || stalled)
                StayWalkFailed(b, inCombat, nowMs);
        }
    }
    return !StayWalkBackingOff(b, nowMs);
}

} // namespace TortoiseBots
