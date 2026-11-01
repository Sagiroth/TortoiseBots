#include "../ai/playerbot/PoolFleePolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::kPoolFleeHeadingCount;
using ai::kPoolFleeNoSpotBackoffMs;
using ai::FleePackPoint;
using ai::PoolFleeHeadingOffsets;
using ai::PoolFleeLandingWakesMob;
using ai::PoolFleeMobInReach;
using ai::PoolFleePackCentroid;
using ai::PoolFleeSearchBackedOff;

int main()
{
    std::cout << "Starting TortoiseBots pool flee policy tests...\n";

    // Straight back first, then alternating sides, widening to 90 degrees.
    {
        float out[kPoolFleeHeadingCount] = {};
        CHECK(PoolFleeHeadingOffsets(out) == kPoolFleeHeadingCount);
        CHECK(out[0] == 0.0f);
        for (int i = 1; i < kPoolFleeHeadingCount; i += 2)
        {
            CHECK(out[i] > 0.0f);
            CHECK(std::fabs(out[i] + out[i + 1]) < 1e-6f);
            if (i > 1)
                CHECK(out[i] > out[i - 2]);
        }
        CHECK(std::fabs(out[kPoolFleeHeadingCount - 2] - 1.5707963f) < 1e-5f);
        // Never a heading that turns back toward the target.
        for (int i = 0; i < kPoolFleeHeadingCount; ++i)
            CHECK(std::fabs(out[i]) <= 1.5707964f);
    }
    std::cout << "  [PASS] fan order: away, then +-22.5 .. +-90 degrees\n";

    // Backoff after a failed search: 2 s, wrap-safe, 0 = none.
    CHECK(!PoolFleeSearchBackedOff(0, 500));
    CHECK(PoolFleeSearchBackedOff(1000, 1000));
    CHECK(PoolFleeSearchBackedOff(1000, 1000 + kPoolFleeNoSpotBackoffMs - 1));
    CHECK(!PoolFleeSearchBackedOff(1000, 1000 + kPoolFleeNoSpotBackoffMs));
    CHECK(PoolFleeSearchBackedOff(0xFFFFFF00u, 0x00000100u)); // 512 ms across the wrap
    CHECK(!PoolFleeSearchBackedOff(0xFFFFFF00u, 0x00001000u));
    std::cout << "  [PASS] no-spot backoff window\n";

    // Unpulled mob: veto landings inside radius+margin that get closer.
    CHECK(PoolFleeLandingWakesMob(15.0f, 25.0f, 18.0f));
    CHECK(PoolFleeLandingWakesMob(19.5f, 25.0f, 18.0f));   // inside the 2 yd margin
    CHECK(!PoolFleeLandingWakesMob(20.5f, 25.0f, 18.0f));  // outside radius+margin
    CHECK(!PoolFleeLandingWakesMob(12.0f, 10.0f, 18.0f));  // already inside, stepping away
    CHECK(!PoolFleeLandingWakesMob(10.0f, 10.0f, 18.0f));  // not closer
    std::cout << "  [PASS] unpulled-mob landing veto\n";

    CHECK(PoolFleeMobInReach(30.0f, 18.0f, 16.0f));
    CHECK(!PoolFleeMobInReach(36.0f, 18.0f, 16.0f));
    std::cout << "  [PASS] mob pre-filter by one step\n";

    // Pack centroid: the average of the live attackers' positions; empty
    // means no pack. A bot between two mobs gets the middle, so the fan
    // finds the gap instead of stepping toward one of them.
    {
        struct Mob { float x, y; bool live; };
        auto posOf = [](Mob const& mob, float& x, float& y) {
            if (!mob.live)
                return false;
            x = mob.x;
            y = mob.y;
            return true;
        };
        std::vector<Mob> none;
        CHECK(!PoolFleePackCentroid(none, posOf).valid);
        std::vector<Mob> split = { { -10.0f, 0.0f, true }, { 10.0f, 0.0f, true } };
        FleePackPoint middle = PoolFleePackCentroid(split, posOf);
        CHECK(middle.valid);
        CHECK(std::fabs(middle.x) < 1e-6f && std::fabs(middle.y) < 1e-6f);
        std::vector<Mob> withCorpse = { { -10.0f, 0.0f, true }, { 10.0f, 0.0f, false }, { 0.0f, 6.0f, true } };
        FleePackPoint pair = PoolFleePackCentroid(withCorpse, posOf);
        CHECK(pair.valid);
        CHECK(std::fabs(pair.x + 5.0f) < 1e-5f && std::fabs(pair.y - 3.0f) < 1e-5f);
        std::cout << "  [PASS] pack centroid averages live attackers only\n";
    }

    std::cout << "All pool flee policy tests passed.\n";
    return 0;
}
