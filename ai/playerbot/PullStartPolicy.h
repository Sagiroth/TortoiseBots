#pragma once

// Pure policy: did a commanded ranged pull get its walk to the firing
// position started?
//
// The order runs the pull's two steps in one AI tick and then judges. The walk
// is a point move: the core registers the movement generator at once, but the
// unit only reads as moving after the next movement tick. Judging on "is the
// unit moving" alone therefore aborted pulls whose walk had just been launched
// ("The tank could not start moving to a firing position for the pull").
// A launched motion counts as started.
//
// Core-free, so tools/test_pull_start_policy.cpp includes it directly.

namespace ai
{
    // A target standing this far above the floor under it is aloft: a hovering boss, a
    // flier over the pull. Its point move lands on an air point the core drops, and the
    // reach only re-picks on the next AI tick, so its walk can never launch inside the
    // order's own tick.
    constexpr float kAirbornePullFloorYd = 5.0f;

    inline bool PullTargetAirborne(bool floorKnown, float targetZ, float floorZ)
    {
        return floorKnown && targetZ - floorZ > kAirbornePullFloorYd;
    }

    struct PullStartFacts
    {
        bool hasTarget = true;       // the pull target is in the world
        bool moving = false;         // the unit already reads as moving
        bool motionLaunched = false; // a point or chase movement generator is set
        bool airborne = false;       // the target hangs more than kAirbornePullFloorYd above the floor under it
        float distance = 0.0f;       // tank to the pull target
        float range = 0.0f;          // the pull's shooting range
    };

    inline bool PullWalkStarted(PullStartFacts const& facts)
    {
        // An airborne target's launch comes on the next movement tick: judging it in the
        // order's own tick refused pulls whose reach had only to re-pick its firing
        // position once more.
        return !facts.hasTarget || facts.moving || facts.motionLaunched ||
            facts.distance <= facts.range || facts.airborne;
    }

    // What the order clears on the tank before its decisions. A pull often
    // comes a second after the last fight ends (the run leader pulls that
    // soon), while the tank sits eating: in an Uldaman run (build 19z11) the
    // meal cast (24005) was live one
    // second before the refused pullback. "reach pull" will not move a bot
    // that is casting (ReachTargetAction::isUseful), so the walk was never
    // launched and the order was refused. A human or commander order beats
    // the meal: the cast is interrupted and the tank stands up. A ranged
    // auto-repeat (the shot itself) is not a cast here and is left alone.
    struct PullReadyFacts
    {
        bool casting = false; // a non-melee, non-auto-repeat spell is being cast
        bool seated = false;  // the tank sits (eating or drinking)
    };

    struct PullReadySteps
    {
        bool interrupt = false;
        bool stand = false;
    };

    inline PullReadySteps ReadyTankForPull(PullReadyFacts const& facts)
    {
        PullReadySteps steps;
        steps.interrupt = facts.casting;
        steps.stand = facts.seated;
        return steps;
    }

    // A ranged pull whose walk did not start after the order's decisions gets
    // one direct "reach pull" before the order is refused: a decision can go
    // to another action (a shout, the stay) and leave the walk for a later
    // tick that a synchronous order never sees. A body pull is never judged.
    inline bool RetryPullWalk(PullStartFacts const& facts, bool bodyPull)
    {
        return !bodyPull && !PullWalkStarted(facts);
    }
}
