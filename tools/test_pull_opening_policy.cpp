#include "../ai/playerbot/PullOpeningPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::DecidePullOpening;
using ai::ParsePullOrderOption;
using ai::PullOpeningInput;
using ai::PullOpeningReason;
using ai::PullOrderOption;

// A tank in sight of a reachable mob 12 yd away, inside a 35 yd shot, with the
// automatic rules on and the walk-in limit at 20 yd.
static PullOpeningInput Base()
{
    PullOpeningInput in;
    in.inLos = true;
    in.pathReachable = true;
    in.distance = 12.0f;
    in.maxRange = 35.0f;
    in.meleeApproach = 20.0f;
    in.autoEnabled = true;
    return in;
}

int main()
{
    std::cout << "Starting TortoiseBots pull opening policy tests...\n";

    // A plain pull of a reachable mob inside the walk-in limit stays a body
    // pull: that is the behaviour before this policy existed.
    {
        auto const d = DecidePullOpening(Base());
        CHECK(!d.wantRanged);
        CHECK(d.reason == PullOpeningReason::BodyPull);
        std::cout << "  [PASS] reachable and near: body pull\n";
    }

    // (a) No path to a mob that is in sight and in range: shoot it.
    {
        PullOpeningInput in = Base();
        in.pathReachable = false;
        auto const d = DecidePullOpening(in);
        CHECK(d.wantRanged);
        CHECK(d.reason == PullOpeningReason::Unreachable);
        std::cout << "  [PASS] unreachable, in sight, in range: ranged\n";
    }

    // The same ledge mob out of sight, or past the shot, is not a ranged pull:
    // there is nothing to shoot yet, and the tank is not sent to a firing
    // position for a mob it cannot reach.
    {
        PullOpeningInput in = Base();
        in.pathReachable = false;
        in.inLos = false;
        CHECK(!DecidePullOpening(in).wantRanged);
        in.inLos = true;
        in.distance = 36.0f;
        CHECK(!DecidePullOpening(in).wantRanged);
        in.distance = 35.0f;
        CHECK(DecidePullOpening(in).wantRanged);
        std::cout << "  [PASS] unreachable needs sight and range (35 yd is in, 36 is out)\n";
    }

    // (b) Farther than the walk-in limit, in sight and in range: shoot.
    {
        PullOpeningInput in = Base();
        in.distance = 25.0f;
        auto const d = DecidePullOpening(in);
        CHECK(d.wantRanged);
        CHECK(d.reason == PullOpeningReason::FarApproach);
        in.distance = 20.0f;
        CHECK(!DecidePullOpening(in).wantRanged);
        in.distance = 20.5f;
        CHECK(DecidePullOpening(in).wantRanged);
        std::cout << "  [PASS] farther than the walk-in limit: ranged (exactly at it: body pull)\n";
    }

    // Far but out of sight: walk, the old way.
    {
        PullOpeningInput in = Base();
        in.distance = 25.0f;
        in.inLos = false;
        CHECK(!DecidePullOpening(in).wantRanged);
        std::cout << "  [PASS] far but out of sight: body pull\n";
    }

    // The walk-in limit is configurable: a short limit shoots a near mob, a
    // long one walks to a far mob.
    {
        PullOpeningInput in = Base();
        in.meleeApproach = 8.0f;
        CHECK(DecidePullOpening(in).wantRanged);
        in.meleeApproach = 40.0f;
        in.distance = 30.0f;
        CHECK(!DecidePullOpening(in).wantRanged);
        std::cout << "  [PASS] the walk-in limit is read from the config\n";
    }

    // (c) The order says ranged: shoot whatever the mob does, in sight or not,
    // near or far, reachable or not.
    {
        PullOpeningInput in = Base();
        in.explicitRanged = true;
        auto d = DecidePullOpening(in);
        CHECK(d.wantRanged && d.reason == PullOpeningReason::Explicit);
        in.inLos = false;
        in.distance = 60.0f;
        in.pathReachable = false;
        d = DecidePullOpening(in);
        CHECK(d.wantRanged && d.reason == PullOpeningReason::Explicit);
        std::cout << "  [PASS] explicit ranged order: always ranged\n";
    }

    // The order wins over the config: RangedPull = 0 turns the automatic
    // rules off, not the player's word.
    {
        PullOpeningInput in = Base();
        in.autoEnabled = false;
        in.explicitRanged = true;
        CHECK(DecidePullOpening(in).wantRanged);
        std::cout << "  [PASS] explicit order is honoured with RangedPull = 0\n";
    }

    // RangedPull = 0: a plain pull walks in on a ledge mob and on a far mob,
    // exactly as before the key existed.
    {
        PullOpeningInput in = Base();
        in.autoEnabled = false;
        in.pathReachable = false;
        CHECK(!DecidePullOpening(in).wantRanged);
        in.pathReachable = true;
        in.distance = 30.0f;
        CHECK(!DecidePullOpening(in).wantRanged);
        std::cout << "  [PASS] RangedPull = 0: no automatic ranged opening\n";
    }

    // A pullback opens from range, as it always did, with the key off too.
    {
        PullOpeningInput in = Base();
        in.pullback = true;
        auto d = DecidePullOpening(in);
        CHECK(d.wantRanged && d.reason == PullOpeningReason::Pullback);
        in.autoEnabled = false;
        in.inLos = false;
        CHECK(DecidePullOpening(in).wantRanged);
        std::cout << "  [PASS] pullback: ranged, unchanged by the key\n";
    }

    // The order names its reason in the answer, and nothing else is named
    // "body-pull".
    {
        CHECK(std::string(ai::PullOpeningReasonName(PullOpeningReason::Explicit)) == "order");
        CHECK(std::string(ai::PullOpeningReasonName(PullOpeningReason::Unreachable)) == "unreachable");
        CHECK(std::string(ai::PullOpeningReasonName(PullOpeningReason::FarApproach)) == "far");
        CHECK(std::string(ai::PullOpeningReasonName(PullOpeningReason::BodyPull)) == "body-pull");
        CHECK(std::string(ai::PullRangedBlockerName(ai::PullRangedBlocker::NoAmmo)) == "no-ammo");
        CHECK(std::string(ai::PullRangedBlockerName(ai::PullRangedBlocker::NoWeapon)) == "no-ranged-weapon");
        CHECK(std::string(ai::PullRangedBlockerName(ai::PullRangedBlocker::None)).empty());
        std::cout << "  [PASS] reasons and blockers have stable names\n";
    }

    // The words of a pull order: [seconds] [ranged].
    {
        PullOrderOption o;
        CHECK(ParsePullOrderOption("", o) && !o.hasSeconds && !o.ranged);
        CHECK(ParsePullOrderOption("10", o) && o.hasSeconds && o.seconds == 10 && !o.ranged);
        CHECK(ParsePullOrderOption("ranged", o) && !o.hasSeconds && o.ranged);
        CHECK(ParsePullOrderOption("  0   RANGED ", o) && o.hasSeconds && o.seconds == 0 && o.ranged);
        CHECK(ParsePullOrderOption("60 ranged", o) && o.seconds == 60 && o.ranged);
        CHECK(ParsePullOrderOption("007", o) && o.seconds == 7);
        CHECK(!ParsePullOrderOption("61", o));
        CHECK(!ParsePullOrderOption("-1", o));
        CHECK(!ParsePullOrderOption("1000", o));
        CHECK(!ParsePullOrderOption("5abc", o));
        CHECK(!ParsePullOrderOption("ranged 5", o));
        CHECK(!ParsePullOrderOption("ranged ranged", o));
        CHECK(!ParsePullOrderOption("5 ranged now", o));
        CHECK(!ParsePullOrderOption("rangedx", o));
        std::cout << "  [PASS] pull order words: [seconds] [ranged], nothing else\n";
    }

    std::cout << "All pull opening policy tests passed.\n";
    return 0;
}
