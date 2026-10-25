#include "../ai/playerbot/policy/raid/ThaddiusPolarityPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsThaddiusPetActive;
using ai::IsThaddiusPhasePet;
using ai::IsThaddiusPhaseTransition;
using ai::ShouldStopPetDps;
using ai::UpstreamThaddiusPolaritySide;

int UpstreamThaddiusTest()
{
    std::cout << "Starting TortoiseBots Thaddius-polarity policy tests...\n";

    // Fake-dead adds (alive flag off OR not-selectable) are not active.
    CHECK(IsThaddiusPetActive(true, false));
    CHECK(!IsThaddiusPetActive(true, true));
    CHECK(!IsThaddiusPetActive(false, false));
    CHECK(IsThaddiusPhasePet(true, false));
    CHECK(IsThaddiusPhasePet(false, true));
    CHECK(!IsThaddiusPhasePet(false, false));
    // Transition only with pets down and Thaddius still shielded.
    CHECK(IsThaddiusPhaseTransition(false, true));
    CHECK(!IsThaddiusPhaseTransition(true, true));
    CHECK(!IsThaddiusPhaseTransition(false, false));
    std::cout << "  [PASS] phase detection honors the fake-death guard\n";

    // Even-HP gate: stop at <=40% while the other pet leads by 3%+.
    CHECK(ShouldStopPetDps(35.0f, 40.0f));
    CHECK(ShouldStopPetDps(40.0f, 43.0f));
    CHECK(!ShouldStopPetDps(41.0f, 90.0f));
    CHECK(!ShouldStopPetDps(35.0f, 37.0f));
    CHECK(!ShouldStopPetDps(35.0f, 35.0f));
    std::cout << "  [PASS] even-HP DPS gate matches donor rule\n";

    // Polarity sides: negative left, positive right, none center.
    CHECK(UpstreamThaddiusPolaritySide(true, false) == 0);
    CHECK(UpstreamThaddiusPolaritySide(false, true) == 1);
    CHECK(UpstreamThaddiusPolaritySide(false, false) == 2);
    CHECK(UpstreamThaddiusPolaritySide(true, true) == 0);
    std::cout << "  [PASS] polarity sides match donor mapping\n";

    std::cout << "All Thaddius-polarity policy tests passed.\n";
    return 0;
}

#include "../ai/playerbot/policy/raid/ThaddiusPolarityPolicy.h"
#include <cassert>
#include <cmath>

int main()
{
    if (UpstreamThaddiusTest()) return 1;
    using namespace ai;
    assert(GetThaddiusPolaritySide(true, false) == ThaddiusPolaritySide::Positive);
    assert(GetThaddiusPolaritySide(false, true) == ThaddiusPolaritySide::Negative);
    assert(GetThaddiusPolaritySide(false, false) == ThaddiusPolaritySide::None);
    assert(GetThaddiusPolaritySide(true, true) == ThaddiusPolaritySide::None);

    ThaddiusPolarityPoint const positive = GetThaddiusPolarityPoint(100.0f, 200.0f, ThaddiusPolaritySide::Positive);
    ThaddiusPolarityPoint const negative = GetThaddiusPolarityPoint(100.0f, 200.0f, ThaddiusPolaritySide::Negative);
    assert(std::fabs(positive.x + negative.x - 200.0f) < 0.001f);
    assert(std::fabs(positive.y + negative.y - 400.0f) < 0.001f);
    assert(std::fabs(std::hypot(positive.x - 100.0f, positive.y - 200.0f) - THADDIUS_POLARITY_SIDE_RADIUS) < 0.001f);
    assert(std::hypot(positive.x - negative.x, positive.y - negative.y) >= 13.0f);

    assert(!ThaddiusPolarityNeedsMove(positive.x + 2.5f, positive.y, positive));
    assert(ThaddiusPolarityNeedsMove(positive.x + 2.51f, positive.y, positive));
    // Opposite arrivals can both sit on the inward edge of their tolerance.
    float const inwardScale = 1.0f - (THADDIUS_POLARITY_ARRIVAL_TOLERANCE - 0.001f) /
        THADDIUS_POLARITY_SIDE_RADIUS;
    auto const p = GetThaddiusPolarityPoint(0, 0, ThaddiusPolaritySide::Positive);
    auto const n = GetThaddiusPolarityPoint(0, 0, ThaddiusPolaritySide::Negative);
    assert(!ThaddiusPolarityNeedsMove(p.x * inwardScale, p.y * inwardScale, p));
    assert(!ThaddiusPolarityNeedsMove(n.x * inwardScale, n.y * inwardScale, n));
    assert(std::hypot((p.x - n.x) * inwardScale, (p.y - n.y) * inwardScale) > 13.0f);
    // Segment interiors, not just endpoints, must clear an opposite charge.
    assert(!ThaddiusSegmentClearsOpposite({-20, 0}, {20, 0}, {0, 0}));
    assert(ThaddiusSegmentClearsOpposite({-20, 15}, {20, 15}, {0, 0}));
    assert(!ThaddiusSegmentClearsOpposite({-20, 13}, {20, 13}, {0, 0}));
    assert(ThaddiusSegmentClearsOpposite({1, 0}, {15, 0}, {0, 0}));
    assert(!ThaddiusSegmentClearsOpposite({1, 0}, {-15, 0}, {0, 0}));
    // Navmesh paths may repeat the start point before the outward segment.
    assert(ThaddiusSegmentClearsOpposite({1, 0}, {1, 0}, {0, 0}));
    // Safe paths may close distance without entering the damage margin.
    assert(ThaddiusSegmentClearsOpposite({30, 0}, {15, 0}, {0, 0}));
    // A normal charge flip points at the occupied old opposite anchor.
    // Reject that route, but never suppress normal/manual movement on failure.
    assert(!ThaddiusSegmentClearsOpposite(p, n, n));
    assert(!ThaddiusSegmentClearsOpposite(n, p, p));
    assert(!ShouldHoldThaddiusPosition(true, true));
    assert(!ShouldHoldThaddiusPosition(true, false));
    assert(!ShouldHoldThaddiusPosition(false, false));
    assert(ShouldHoldThaddiusPosition(false, true));
    return 0;
}
