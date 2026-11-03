#pragma once

// Pure policy for bot walk/run state transitions (no core types, so
// tools/test_walk_mode_policy.cpp builds it standalone).
//
// 1.12 clients keep a unit's observed walk/run mode as persistent state and
// treat the mode the server's spline launch leaves behind as the truth: the
// launch sends the change and then re-sends the pre-launch mode for player
// units, so a run spline launched while the bot's movement flags still say
// walk leaves every watching client in walk mode until it reloads. Two rules
// keep a bot out of that state:
//
//  - a walk flag an action has set this tick rides that same tick's dispatch
//    (never dropped while the bot still walks);
//  - leftover walk flags are cleared before the next tick's moves are
//    dispatched, so nothing is ever launched on flags that still walk.
//
// The master's walking is mimicked only through the dispatch's own gate
// (master walks and is within WalkDistance, out of combat). No bot movement
// flag is written for the mimic itself.

namespace ai::WalkModePolicy
{
    // Whether the movement about to be dispatched walks. Either source walks,
    // so a run dispatch is impossible the moment the flags walk: it is the
    // caller's reconcile that clears leftover walk flags before dispatch.
    inline bool DispatchWalks(bool masterWalking, bool botWalkFlag)
    {
        return masterWalking || botWalkFlag;
    }
}
