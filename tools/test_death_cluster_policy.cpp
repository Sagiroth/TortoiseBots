#include "../ai/playerbot/DeathClusterPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::DeathAvoidanceEscalated;
using ai::IsDeathPositionAvoided;
using ai::kDeathAvoidEscapes;
using ai::kDeathAvoidMs;
using ai::kDeathAvoidRadiusYd;
using ai::kDeathAvoidWindowMs;
using ai::NextDeathEscapeCount;

namespace
{
    std::uint32_t const NOW = 1'000'000;
    // Tirisfal loop shape: escapes ~3-11 min apart, same 300 yd camp, map 0.
    std::uint32_t const MAP = 0;
    float const X = -550.0f;
    float const Y = 2340.0f;
}

int main()
{
    std::cout << "Starting TortoiseBots death-cluster escalation tests...\n";

    // -------------------------------------------------------------
    // Test 1: a lone escape only blacklists the kind, no escalation
    // -------------------------------------------------------------
    {
        std::uint32_t count = NextDeathEscapeCount(0, NOW, 0);
        CHECK(count == 1);
        CHECK(!DeathAvoidanceEscalated(count));
        CHECK(kDeathAvoidEscapes == 2);
    }

    // -------------------------------------------------------------
    // Test 2: a second escape inside the hour escalates to avoidance
    // -------------------------------------------------------------
    {
        std::uint32_t count = NextDeathEscapeCount(1, NOW, NOW - 11 * 60 * 1000); // median loop gap 10.7 min
        CHECK(count == 2);
        CHECK(DeathAvoidanceEscalated(count));
        // A third escape inside the window stays escalated.
        count = NextDeathEscapeCount(count, NOW, NOW - 5 * 60 * 1000);
        CHECK(count == 3);
        CHECK(DeathAvoidanceEscalated(count));
    }

    // -------------------------------------------------------------
    // Test 3: an escape after the window starts a fresh streak
    // -------------------------------------------------------------
    {
        std::uint32_t count = NextDeathEscapeCount(2, NOW, NOW - kDeathAvoidWindowMs - 1);
        CHECK(count == 1);
        CHECK(!DeathAvoidanceEscalated(count));
        // Exactly on the window edge still belongs to the loop.
        count = NextDeathEscapeCount(1, NOW, NOW - kDeathAvoidWindowMs);
        CHECK(count == 2);
        CHECK(DeathAvoidanceEscalated(count));
    }

    // -------------------------------------------------------------
    // Test 4: the avoided spot covers the camp, nothing else
    // -------------------------------------------------------------
    {
        std::uint32_t const expiry = NOW + kDeathAvoidMs;
        CHECK(IsDeathPositionAvoided(MAP, X, Y, MAP, X, Y, NOW, expiry)); // the death itself
        CHECK(IsDeathPositionAvoided(MAP, X + 100.0f, Y - 100.0f, MAP, X, Y, NOW, expiry)); // same camp
        CHECK(IsDeathPositionAvoided(MAP, X + kDeathAvoidRadiusYd, Y, MAP, X, Y, NOW, expiry)); // edge counts
        CHECK(!IsDeathPositionAvoided(MAP, X + kDeathAvoidRadiusYd + 1.0f, Y, MAP, X, Y, NOW, expiry));
        CHECK(!IsDeathPositionAvoided(MAP + 1, X, Y, MAP, X, Y, NOW, expiry)); // another map
        CHECK(!IsDeathPositionAvoided(MAP, X, Y, MAP, X, Y, expiry + 1, expiry)); // expired
        CHECK(!IsDeathPositionAvoided(MAP, X, Y, MAP, X, Y, NOW, 0)); // no avoidance stored
    }

    // -------------------------------------------------------------
    // Test 5: ms-clock wraparound does not fake or drop a streak
    // -------------------------------------------------------------
    {
        std::uint32_t const nearWrap = 0xFFFFFF00u;
        std::uint32_t count = NextDeathEscapeCount(1, 60000u, nearWrap); // ~65 s later across the wrap
        CHECK(count == 2);
        CHECK(!IsDeathPositionAvoided(MAP, X, Y, MAP, X, Y, 10u, 5u));
    }

    std::cout << "All death-cluster escalation tests passed.\n";
    return 0;
}
