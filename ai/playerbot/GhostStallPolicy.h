#pragma once

#include <cstdint>

namespace ai
{
    // Ghost-stall corpse-run policy (m-stuck §5.2 / finding 4): the ghost's
    // corpse-run spline is re-dispatched from IDLE every tick and never ridden
    // (ghost_moves.csv: 2187/2189 rows before=IDLE(0)/after=POINT(8) with
    // distAfter == distBefore, 348/382 bots on a byte-identical from-position;
    // 263/500 bots > 5 min ghost, 111 ghost-hours in 4 h on night2-4h).
    //
    // Two defects combine:
    //  1. FindCorpseAction re-dispatches MoveTo every tick while the launched
    //     POINT spline is still healthy: MotionMaster::Mutate stacks a second
    //     POINT generator on top instead of resuming the first, and each new
    //     dispatch resets the ghost to the start of the route, so no spline
    //     ever advances. The fix is to keep riding while the motion stack
    //     still carries a live POINT generator toward (nearly) the same
    //     destination (ShouldKeepRidingCorpseRun below).
    //  2. The only corpse-leg rescue that fires without walking - the
    //     wait-then-teleport in FindCorpseAction - requires
    //     !AllowActivity(DETAILED_MOVE_ACTIVITY). With
    //     AiPlayerbot.DisableActivityPriorities=1 every bot is always fully
    //     active (GetPriorityType returns HAS_REAL_PLAYER_MASTER), so the
    //     branch never fires and the stall walks out the 8-min yield / 10-min
    //     GY teleport. The 60-s stall rule below (ShouldGhostStallTeleport)
    //     fires on deadTime/distance only, independent of the activity gate.
    //
    // Pure decision rule, no core includes: callers own the motion-stack read
    // (IDLE_MOTION_TYPE=0, POINT_MOTION_TYPE=8 per core MotionMaster.h) and
    // the manual time/int anchor values.
    //
    // Owned/hired bots (HasActivePlayerMaster) are out of scope: their death
    // handling stays exactly as today - the wait-for-master gate and the
    // follow path are untouched. Both helpers take hasActivePlayerMaster and
    // return "do nothing" for it, so a caller cannot accidentally apply the
    // pool-bot rescue to an owned bot.

    // Ghost walk that counts as "moving": a run that closes the distance to
    // the corpse slowly keeps its clock reset, only a stopped one trips.
    // Matches the existing kGhostStallProgressYd in DeadValues.cpp.
    float const kGhostStallProgressYd = 5.0f;

    // A ghost that stops closing for this long is not corpse-running any
    // more. Matches the existing kGhostStallSec in DeadValues.cpp (which
    // hands the run to the spirit healer) and kSpiritHealerStallSec in
    // ReviveFromCorpseAction.cpp (which teleports the GY walk).
    std::uint32_t const kGhostStallTeleportSec = 60;

    // A re-dispatch closer than this to the in-flight destination is the same
    // run, not a new decision: the corpse leg re-aims at the corpse (or the
    // flee/sidestep point within the 34 yd reclaim radius) every tick, so the
    // target jitters by yards while the route is identical. 10 yd absorbs the
    // jitter without pinning a genuinely re-aimed run (master leg, portal
    // leg, reclaim-range arrival) to a stale spline.
    float const kGhostKeepRidingRadiusYd = 10.0f;

    // Keep riding the in-flight POINT spline instead of re-dispatching MoveTo.
    // didLaunchThisTick: a POINT generator appeared on the stack as a result
    // of THIS tick's MoveTo (movegenBefore == IDLE, movegenAfter == POINT).
    // That is the launch tick, not a stall - the spline has not had a tick to
    // advance yet. Only a POINT generator that was already there when the
    // tick started (movegenBefore == POINT) proves a previous dispatch is
    // still in flight.
    inline bool ShouldKeepRidingCorpseRun(bool hasActivePlayerMaster, bool isGhost,
        int movegenBefore, int pointMotionType, float distToInflightYd)
    {
        if (hasActivePlayerMaster)
            return false;
        if (!isGhost)
            return false;
        if (movegenBefore != pointMotionType)
            return false;
        return distToInflightYd <= kGhostKeepRidingRadiusYd;
    }

    // Stall teleport independent of the activity-priority gate: fires on
    // deadTime/distance only. Same shape as the walk-to-graveyard stall rule
    // (ReviveFromCorpseAction.cpp): the 60-s anchor must already have been
    // armed by real progress bookkeeping (anchor != 0) and the ghost must
    // still have ground to cover (inside the core reclaim radius the reclaim
    // path fires without walking, so it never counts).
    inline bool ShouldGhostStallTeleport(bool hasActivePlayerMaster, bool insideReclaimRange,
        std::int64_t anchor, std::int64_t now, std::uint32_t stallSec = kGhostStallTeleportSec)
    {
        if (hasActivePlayerMaster)
            return false;
        if (insideReclaimRange)
            return false;
        if (anchor == 0)
            return false;
        return (now - anchor) >= (std::int64_t)stallSec;
    }
}
