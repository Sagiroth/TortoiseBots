#include "../ai/playerbot/PullStartPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::PullStartFacts;
using ai::PullWalkStarted;
using ai::PullReadyFacts;
using ai::PullReadySteps;
using ai::PullTargetAirborne;
using ai::ReadyTankForPull;
using ai::RetryPullWalk;

static void TestWalkStartedAndRetry()
{
    // Mob 38 yd away, pull range 30, tank standing, nothing launched: not started.
    PullStartFacts facts;
    facts.distance = 38.0f;
    facts.range = 30.0f;
    CHECK(!PullWalkStarted(facts));

    // The walk was launched this tick but the unit does not read as moving yet.
    facts.motionLaunched = true;
    CHECK(PullWalkStarted(facts));

    // Already moving.
    facts = PullStartFacts();
    facts.distance = 38.0f;
    facts.range = 30.0f;
    facts.moving = true;
    CHECK(PullWalkStarted(facts));

    // Already in range: the shot is ready, nothing to walk.
    facts = PullStartFacts();
    facts.distance = 30.0f;
    facts.range = 30.0f;
    CHECK(PullWalkStarted(facts));

    // The target is gone: nothing to judge.
    facts = PullStartFacts();
    facts.hasTarget = false;
    facts.distance = 99.0f;
    CHECK(PullWalkStarted(facts));

    // A ranged pull not started after the decisions: one direct reach pull.
    facts = PullStartFacts();
    facts.distance = 54.7f;
    facts.range = 29.5f;
    CHECK(RetryPullWalk(facts, false));
    // A body pull is never judged, and a started walk needs no retry.
    CHECK(!RetryPullWalk(facts, true));
    facts.motionLaunched = true;
    CHECK(!RetryPullWalk(facts, false));

    // A ground target in the same state still fails the check.
    facts = PullStartFacts();
    facts.distance = 54.7f;
    facts.range = 29.5f;
    CHECK(!PullWalkStarted(facts));
}

// A boss hovering over her altar (10 yd above the floor, 51 yd from the tank): the ranged
// pullback's reach picked an air point for its firing position, the core dropped the move,
// and the order judged "walk started" in the same tick refused the pull ("The tank could
// not start moving to a firing position for the pull") into a pause. The launch comes on
// the next AI tick: an airborne target's pull is accepted, not refused.
static void TestAirTargetPullCountsNextTickLaunch()
{
    // The floor under the target is the step's floor; the target hangs above it.
    CHECK(PullTargetAirborne(true, 144.6f, 134.3f));
    CHECK(!PullTargetAirborne(true, 134.3f, 134.3f));
    CHECK(!PullTargetAirborne(true, 135.0f, 134.3f)); // within the 5 yd bound
    CHECK(!PullTargetAirborne(false, 200.0f, 0.0f));  // no floor read: not aloft

    PullStartFacts facts;
    facts.distance = 54.7f;
    facts.range = 29.5f;
    facts.airborne = true;
    facts.reachAttempted = true;
    // The reach ran and launched nothing yet: the next tick's launch is accepted.
    CHECK(PullWalkStarted(facts));
    CHECK(!RetryPullWalk(facts, false));
}

// Codex review t1105c round 2 (medium): airborne geometry explains a deferred launch, it
// does not prove the walk was attempted. When the queued decision never ran `reach pull`
// (another action displaced it) the air target must not pass as started: the direct
// retry runs, and only after it is the next tick's launch awaited.
static void TestAirTargetNeedsAnAttemptedReach()
{
    PullStartFacts facts;
    facts.distance = 54.7f;
    facts.range = 29.5f;
    facts.airborne = true;
    facts.reachAttempted = false;
    CHECK(!PullWalkStarted(facts));
    CHECK(RetryPullWalk(facts, false));
    CHECK(!RetryPullWalk(facts, true)); // a body pull is never judged
    // After the direct reach pull ran, the air target gets the next tick.
    facts.reachAttempted = true;
    CHECK(PullWalkStarted(facts));
    CHECK(!RetryPullWalk(facts, false));
    // An attempted reach does not excuse a ground target.
    facts.airborne = false;
    CHECK(!PullWalkStarted(facts));
    // No-attempt air target still passes when really moving, launched or in range.
    facts.airborne = true;
    facts.reachAttempted = false;
    facts.moving = true;
    CHECK(PullWalkStarted(facts));
    facts.moving = false;
    facts.motionLaunched = true;
    CHECK(PullWalkStarted(facts));
    facts.motionLaunched = false;
    facts.distance = 20.0f;
    CHECK(PullWalkStarted(facts));
}

static void TestReadyTankForPull()
{
    // Uldaman run (build 19z11): the pullback came a second after the fight,
    // the tank sitting with the meal cast (24005) live. The order interrupts
    // the cast and stands the tank up before its decisions.
    PullReadyFacts ready;
    ready.casting = true;
    ready.seated = true;
    PullReadySteps steps = ReadyTankForPull(ready);
    CHECK(steps.interrupt && steps.stand);
    steps = ReadyTankForPull(PullReadyFacts());
    CHECK(!steps.interrupt && !steps.stand);
    ready = PullReadyFacts();
    ready.seated = true;
    steps = ReadyTankForPull(ready);
    CHECK(!steps.interrupt && steps.stand);
}

int main()
{
    TestWalkStartedAndRetry();
    TestAirTargetPullCountsNextTickLaunch();
    TestAirTargetNeedsAnAttemptedReach();
    TestReadyTankForPull();

    std::cout << "pull start policy: ok\n";
    return 0;
}
