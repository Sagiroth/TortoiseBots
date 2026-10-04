#include "../ai/playerbot/CombatSpreadPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::FleeHeadingDistance;
using ai::IsFleeHeadingFree;
using ai::IsSpreadExemptOwned;
using ai::IsSpreadOnCooldown;
using ai::kFleeAngleEmpty;
using ai::kSpreadStepCooldownMs;
using ai::ShouldCombatSpread;

int main()
{
    std::cout << "Starting TortoiseBots combat-spread policy tests...\n";

    // Heading distance wraps: 0 and a full turn are the same heading, and
    // -PI/+PI are the same point.
    CHECK(FleeHeadingDistance(0.0f, 0.0f) == 0.0f);
    CHECK(FleeHeadingDistance(0.0f, 6.2831853072f) < 0.001f);
    CHECK(FleeHeadingDistance(3.1415926536f, -3.1415926536f) < 0.001f);
    CHECK(FleeHeadingDistance(0.0f, 3.1415926536f) > 3.14f);
    std::cout << "  [PASS] heading distance wraps at a full turn\n";

    // A repeated flee heading is vetoed: same vector and near neighbours
    // (within 45deg) are skipped so the bot steps elsewhere.
    float past[2] = { 0.0f, kFleeAngleEmpty };
    CHECK(!IsFleeHeadingFree(0.0f, past, 2));
    CHECK(!IsFleeHeadingFree(0.5f, past, 2));
    CHECK(IsFleeHeadingFree(1.0f, past, 2));
    CHECK(IsFleeHeadingFree((float)M_PI, past, 2));
    std::cout << "  [PASS] repeated flee heading vetoed within 45deg\n";

    // Empty memory vetoes nothing: first flee after a fresh start or a
    // teleport (values cleared) keeps the full ring.
    float empty[2] = { kFleeAngleEmpty, kFleeAngleEmpty };
    CHECK(IsFleeHeadingFree(0.0f, empty, 2));
    CHECK(IsFleeHeadingFree(2.0f, empty, 2));
    std::cout << "  [PASS] empty flee memory vetoes nothing\n";

    // Second slot vetoes too: alternating A/B/A no longer oscillates.
    float both[2] = { 0.0f, (float)M_PI / 2.0f };
    CHECK(!IsFleeHeadingFree(0.1f, both, 2));
    CHECK(!IsFleeHeadingFree(1.6f, both, 2));
    CHECK(IsFleeHeadingFree((float)M_PI, both, 2));
    std::cout << "  [PASS] both remembered headings veto\n";

    // Spread gate: combat only, pool bots only, no hold order.
    CHECK(ShouldCombatSpread(true, false, false, false, false, false));
    CHECK(!ShouldCombatSpread(false, false, false, false, false, false));
    CHECK(!ShouldCombatSpread(true, true, false, false, false, false));
    CHECK(!ShouldCombatSpread(true, false, true, false, false, false));
    CHECK(!ShouldCombatSpread(true, false, false, true, false, false));
    CHECK(!ShouldCombatSpread(true, false, false, false, true, false));
    CHECK(!ShouldCombatSpread(true, false, false, false, false, true));
    std::cout << "  [PASS] combat-only pool-bot spread gate\n";

    // Owned-bot answer: a live master exempts, and so does the owner record
    // when the master is offline or on another character. Pool bots pass.
    CHECK(IsSpreadExemptOwned(true, false));
    CHECK(IsSpreadExemptOwned(false, true));
    CHECK(IsSpreadExemptOwned(true, true));
    CHECK(!IsSpreadExemptOwned(false, false));
    std::cout << "  [PASS] offline-master owned bots stay exempt\n";

    // Re-step throttle: no dispatch yet never throttles; a step inside the
    // window holds; a step past it goes. Wrap-safe at the 32-bit clock edge.
    CHECK(!IsSpreadOnCooldown(5000u, 0u));
    CHECK(IsSpreadOnCooldown(5000u, 5000u));
    CHECK(IsSpreadOnCooldown(5000u + kSpreadStepCooldownMs - 1, 5000u));
    CHECK(!IsSpreadOnCooldown(5000u + kSpreadStepCooldownMs, 5000u));
    CHECK(IsSpreadOnCooldown(10u, 0xFFFFFF00u));
    CHECK(!IsSpreadOnCooldown(100000u, 0xFFFFFF00u));
    std::cout << "  [PASS] spread re-step throttle\n";
    std::cout << "TortoiseBots combat-spread policy tests passed.\n";
    return 0;
}
