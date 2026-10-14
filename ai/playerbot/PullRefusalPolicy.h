#pragma once

// Pull refusal codes (`.bot action pull|pullback`, `.bot cmd pull|pullback`).
//
// The pull needs a tank that is out of combat (a tank already fighting an add
// cannot walk up to the next mob). That refusal used to answer `no-tank`, the
// same code as "there is no tank at all", so a client could not tell "retry in
// a moment" from "give up". When no executor is free but a tank-capable bot is
// alive and in combat, the answer is `tank-busy`. When the pull is refused is
// unchanged: only the code and text differ.
//
// Pure, core-free, so tools/test_pull_refusal_policy.cpp includes it directly.

namespace ai
{
    inline char const* PullNoExecutorCode(bool tankCandidateInCombat)
    {
        return tankCandidateInCombat ? "tank-busy" : "no-tank";
    }

    inline char const* PullNoExecutorText(bool tankCandidateInCombat)
    {
        return tankCandidateInCombat
            ? "The tank is in combat. Wait for the fight to end (or send an attack order instead) and pull again."
            : "No live tank-capable owned party bot can pull this target.";
    }
}
