#include "../ai/playerbot/DeathClusterPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::AddDeathAvoidSpot;
using ai::DeathAvoidDurationMs;
using ai::DeathAvoidRadiusYd;
using ai::DeathAvoidSpot;
using ai::DeathAvoidanceEscalated;
using ai::DeathSpotActive;
using ai::IsDeathGatedPurpose;
using ai::IsDeathPositionAvoided;
using ai::kDeathAvoidEscapes;
using ai::kDeathAvoidLowMs;
using ai::kDeathAvoidMs;
using ai::kDeathAvoidRadiusLowYd;
using ai::kDeathAvoidRadiusYd;
using ai::kDeathAvoidSpots;
using ai::kDeathAvoidWindowMs;
using ai::NextDeathEscapeCount;

namespace
{
    std::uint32_t const NOW = 1'000'000;
    // Tirisfal loop shape: escapes ~3-11 min apart, same 300 yd camp, map 0.
    std::uint32_t const MAP = 0;
    float const X = -550.0f;
    float const Y = 2340.0f;

    // Fake purpose ids mirroring TravelDestinationPurpose (bitmask enum).
    std::uint32_t const GRIND = 1u << 12;
    std::uint32_t const QUEST_OBJ1 = 1u << 1;
    std::uint32_t const QUEST_OBJ4 = 1u << 4;
    std::uint32_t const QUEST_ALL = QUEST_OBJ1 | (1u << 2) | (1u << 3) | QUEST_OBJ4;
    std::uint32_t const QUEST_GIVER = 1u << 0;
    std::uint32_t const QUEST_TAKER = 1u << 5;
    std::uint32_t const VENDOR = 1u << 9;

    void AvoidOne(DeathAvoidSpot* spots, float x, float y, std::uint32_t nowMs, std::uint32_t level)
    {
        AddDeathAvoidSpot(spots, kDeathAvoidSpots, MAP, x, y, nowMs, DeathAvoidDurationMs(level),
            DeathAvoidRadiusYd(level));
    }
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
    // Test 4: one avoided spot covers its camp, nothing else
    // -------------------------------------------------------------
    {
        DeathAvoidSpot spots[kDeathAvoidSpots] = {};
        AvoidOne(spots, X, Y, NOW, 6);
        CHECK(IsDeathPositionAvoided(MAP, X + 100.0f, Y - 100.0f, NOW, spots, kDeathAvoidSpots)); // same camp
        CHECK(IsDeathPositionAvoided(MAP, X + kDeathAvoidRadiusYd, Y, NOW, spots, kDeathAvoidSpots)); // edge counts
        CHECK(!IsDeathPositionAvoided(MAP, X + kDeathAvoidRadiusYd + 1.0f, Y, NOW, spots, kDeathAvoidSpots));
        CHECK(!IsDeathPositionAvoided(MAP + 1, X, Y, NOW, spots, kDeathAvoidSpots)); // another map
        CHECK(!IsDeathPositionAvoided(MAP, X, Y, NOW + kDeathAvoidMs + 1, spots, kDeathAvoidSpots)); // expired
        DeathAvoidSpot empty[kDeathAvoidSpots] = {};
        CHECK(!IsDeathPositionAvoided(MAP, X, Y, NOW, empty, kDeathAvoidSpots)); // nothing stored
    }

    // -------------------------------------------------------------
    // Test 5: ms-clock wraparound keeps expiry and streak correct
    // -------------------------------------------------------------
    {
        std::uint32_t const nearWrap = 0xFFFFFF00u;
        std::uint32_t count = NextDeathEscapeCount(1, 60000u, nearWrap); // ~65 s later across the wrap
        CHECK(count == 2);
        // Expiry just past the wrap is still live just before it, dead just after.
        DeathAvoidSpot spots[kDeathAvoidSpots] = {};
        AddDeathAvoidSpot(spots, kDeathAvoidSpots, MAP, X, Y, nearWrap, 120000u, kDeathAvoidRadiusYd);
        std::uint32_t const expiry = nearWrap + 120000u; // wraps to a small integer
        CHECK(expiry < nearWrap); // the test really wraps
        CHECK(IsDeathPositionAvoided(MAP, X, Y, expiry - 1, spots, kDeathAvoidSpots));
        CHECK(!IsDeathPositionAvoided(MAP, X, Y, expiry, spots, kDeathAvoidSpots));
        CHECK(!IsDeathPositionAvoided(MAP, X, Y, expiry + 1, spots, kDeathAvoidSpots));
        CHECK(DeathSpotActive(expiry - 1, expiry));
        CHECK(!DeathSpotActive(expiry, expiry));
    }

    // -------------------------------------------------------------
    // Test 6: lowbies avoid a smaller camp for a shorter while
    // -------------------------------------------------------------
    {
        CHECK(kDeathAvoidRadiusLowYd == 100.0f);
        CHECK(kDeathAvoidLowMs == 15u * 60u * 1000u);
        CHECK(DeathAvoidRadiusYd(1) == kDeathAvoidRadiusLowYd);
        CHECK(DeathAvoidRadiusYd(5) == kDeathAvoidRadiusLowYd);
        CHECK(DeathAvoidRadiusYd(6) == kDeathAvoidRadiusYd);
        CHECK(DeathAvoidDurationMs(5) == kDeathAvoidLowMs);
        CHECK(DeathAvoidDurationMs(6) == kDeathAvoidMs);
        DeathAvoidSpot spots[kDeathAvoidSpots] = {};
        AvoidOne(spots, X, Y, NOW, 3);
        // Same camp inside the small radius, the far side of a 300 yd camp free.
        CHECK(IsDeathPositionAvoided(MAP, X + 50.0f, Y, NOW, spots, kDeathAvoidSpots));
        CHECK(!IsDeathPositionAvoided(MAP, X + 250.0f, Y, NOW, spots, kDeathAvoidSpots));
        CHECK(!IsDeathPositionAvoided(MAP, X, Y, NOW + kDeathAvoidLowMs + 1, spots, kDeathAvoidSpots));
    }

    // -------------------------------------------------------------
    // Test 7: up to three spots; a fourth replaces the oldest live one
    // -------------------------------------------------------------
    {
        CHECK(kDeathAvoidSpots == 3);
        DeathAvoidSpot spots[kDeathAvoidSpots] = {};
        AvoidOne(spots, 0.0f, 0.0f, NOW, 6); // A
        AvoidOne(spots, 1000.0f, 0.0f, NOW + 1000, 6); // B
        AvoidOne(spots, 2000.0f, 0.0f, NOW + 2000, 6); // C
        CHECK(IsDeathPositionAvoided(MAP, 0.0f, 0.0f, NOW + 3000, spots, kDeathAvoidSpots));
        CHECK(IsDeathPositionAvoided(MAP, 1000.0f, 0.0f, NOW + 3000, spots, kDeathAvoidSpots));
        CHECK(IsDeathPositionAvoided(MAP, 2000.0f, 0.0f, NOW + 3000, spots, kDeathAvoidSpots));
        AvoidOne(spots, 3000.0f, 0.0f, NOW + 3000, 6); // D replaces oldest (A)
        CHECK(!IsDeathPositionAvoided(MAP, 0.0f, 0.0f, NOW + 3000, spots, kDeathAvoidSpots));
        CHECK(IsDeathPositionAvoided(MAP, 1000.0f, 0.0f, NOW + 3000, spots, kDeathAvoidSpots));
        CHECK(IsDeathPositionAvoided(MAP, 2000.0f, 0.0f, NOW + 3000, spots, kDeathAvoidSpots));
        CHECK(IsDeathPositionAvoided(MAP, 3000.0f, 0.0f, NOW + 3000, spots, kDeathAvoidSpots));
        // An expired slot is reused before evicting a live one.
        DeathAvoidSpot mixed[kDeathAvoidSpots] = {};
        AvoidOne(mixed, 0.0f, 0.0f, NOW - kDeathAvoidMs - 1000, 6); // long expired
        AvoidOne(mixed, 1000.0f, 0.0f, NOW, 6);
        AvoidOne(mixed, 2000.0f, 0.0f, NOW, 6);
        CHECK(IsDeathPositionAvoided(MAP, 1000.0f, 0.0f, NOW, mixed, kDeathAvoidSpots));
        CHECK(IsDeathPositionAvoided(MAP, 2000.0f, 0.0f, NOW, mixed, kDeathAvoidSpots));
    }

    // -------------------------------------------------------------
    // Test 8: purpose gate is a bitmask, not a range
    // -------------------------------------------------------------
    {
        CHECK(IsDeathGatedPurpose(GRIND, GRIND, QUEST_ALL));
        CHECK(IsDeathGatedPurpose(QUEST_OBJ1, GRIND, QUEST_ALL));
        CHECK(IsDeathGatedPurpose(QUEST_OBJ4, GRIND, QUEST_ALL));
        CHECK(IsDeathGatedPurpose(QUEST_ALL, GRIND, QUEST_ALL)); // composite still matches
        CHECK(!IsDeathGatedPurpose(QUEST_GIVER, GRIND, QUEST_ALL));
        CHECK(!IsDeathGatedPurpose(QUEST_TAKER, GRIND, QUEST_ALL));
        CHECK(!IsDeathGatedPurpose(VENDOR, GRIND, QUEST_ALL));
        // Call sites pass objectives + quest NPCs: hand-ins in a camp are gated.
        CHECK(IsDeathGatedPurpose(QUEST_GIVER, GRIND, QUEST_ALL | QUEST_GIVER | QUEST_TAKER));
        CHECK(IsDeathGatedPurpose(QUEST_TAKER, GRIND, QUEST_ALL | QUEST_GIVER | QUEST_TAKER));
        CHECK(!IsDeathGatedPurpose(VENDOR, GRIND, QUEST_ALL | QUEST_GIVER | QUEST_TAKER));
    }

    std::cout << "All death-cluster escalation tests passed.\n";
    return 0;
}
