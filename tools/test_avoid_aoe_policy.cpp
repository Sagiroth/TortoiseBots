#include "../ai/playerbot/AvoidAoePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::AoeEscapeStep;
using ai::IsAvoidableAoeRadius;
using ai::kMaxAoeAvoidRadiusYd;
using ai::MeleeAoeCandidates;
using ai::MeleeAoeLandingInRange;
using ai::RangedAoeCandidates;
using ai::RangedAoeLandingInRange;

int main()
{
    std::cout << "Starting TortoiseBots avoid-aoe policy tests...\n";

    // Radius gate: positive and within the 15yd cap.
    CHECK(IsAvoidableAoeRadius(5.0f));
    CHECK(IsAvoidableAoeRadius(15.0f));
    CHECK(!IsAvoidableAoeRadius(0.0f));
    CHECK(!IsAvoidableAoeRadius(-1.0f));
    CHECK(!IsAvoidableAoeRadius(15.1f));
    CHECK(kMaxAoeAvoidRadiusYd == 15.0f);
    std::cout << "  [PASS] radius cap gates all three sensor cases\n";

    // Melee order: strafes first, straight away third; tanking appends
    // the two fallbacks.
    {
        float out[5] = {};
        CHECK(MeleeAoeCandidates(false, out) == 3);
        CHECK(out[0] > 1.57f && out[0] < 1.58f);
        CHECK(out[1] < -1.57f && out[1] > -1.58f);
        CHECK(out[2] > 3.14f && out[2] < 3.15f);
        CHECK(MeleeAoeCandidates(true, out) == 5);
    }
    std::cout << "  [PASS] melee strafe-first order\n";

    // Ranged order: strafes, away, toward, away-from-hazard.
    {
        float out[5] = {};
        CHECK(RangedAoeCandidates(out) == 5);
        CHECK(out[0] > 0.0f && out[1] < 0.0f);
        CHECK(out[2] > 3.14f);
        CHECK(out[3] == 0.0f);
    }
    std::cout << "  [PASS] ranged strafe-first order\n";

    // Band tests: melee stays close, ranged stays in the cast band.
    CHECK(MeleeAoeLandingInRange(5.0f, 8.0f));
    CHECK(!MeleeAoeLandingInRange(8.1f, 8.0f));
    CHECK(RangedAoeLandingInRange(20.0f, 8.0f, 30.0f));
    CHECK(!RangedAoeLandingInRange(7.9f, 8.0f, 30.0f));
    CHECK(!RangedAoeLandingInRange(30.1f, 8.0f, 30.0f));
    std::cout << "  [PASS] stay-in-range band tests\n";

    // Escape step: just past the edge, capped at flee distance.
    CHECK(AoeEscapeStep(5.0f, 15.0f) == 6.0f);
    CHECK(AoeEscapeStep(20.0f, 15.0f) == 15.0f);
    std::cout << "  [PASS] escape step caps at flee distance\n";

    std::cout << "All avoid-aoe policy tests passed.\n";
    return 0;
}
