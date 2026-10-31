// Standalone test for the order-focus policy
// (ai/playerbot/policy/travel/OrderFocusPolicy.h): unreachable workshop
// points, the drop-mark rule and the short re-pick park.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_order_focus_policy.cpp -o /tmp/test_order_focus_policy
//   /tmp/test_order_focus_policy

#include "../ai/playerbot/policy/travel/OrderFocusPolicy.h"

#include <cstdint>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::AddUnreachableFocusSpot;
using ai::FocusDropMarksSpot;
using ai::IsFocusPointAvoided;
using ai::kUnreachableFocusMs;
using ai::kUnreachableFocusRadiusYd;
using ai::kUnreachableFocusSpots;
using ai::ORDER_FOCUS_PARK_SECONDS;
using ai::UnreachableFocusSpot;

int main()
{
    std::cout << "Starting TortoiseBots order focus policy tests...\n";

    CHECK(ORDER_FOCUS_PARK_SECONDS == 60);

    // Only a still-distant workshop drop marks the point. Inside smelt
    // range the bot crafts from where it stands; other purposes never
    // feed this book.
    CHECK(FocusDropMarksSpot(true, 336.0f));
    CHECK(FocusDropMarksSpot(true, 8.0f));
    CHECK(!FocusDropMarksSpot(true, 7.9f));
    CHECK(!FocusDropMarksSpot(false, 336.0f));

    std::uint32_t const NOW = 1'000'000;
    std::uint32_t const MAP = 1;

    {
        // A marked forge is refused inside its radius, a forge across
        // the zone is not, and another map never matches.
        UnreachableFocusSpot spots[kUnreachableFocusSpots] = {};
        AddUnreachableFocusSpot(spots, kUnreachableFocusSpots, MAP, -2722.0f, 13775.0f, NOW);
        CHECK(IsFocusPointAvoided(MAP, -2722.0f, 13775.0f, NOW + 1000, spots, kUnreachableFocusSpots));
        CHECK(IsFocusPointAvoided(MAP, -2722.0f + kUnreachableFocusRadiusYd - 0.5f, 13775.0f, NOW + 1000, spots, kUnreachableFocusSpots));
        CHECK(!IsFocusPointAvoided(MAP, -2700.0f, 13775.0f, NOW + 1000, spots, kUnreachableFocusSpots));
        CHECK(!IsFocusPointAvoided(0, -2722.0f, 13775.0f, NOW + 1000, spots, kUnreachableFocusSpots));
        // Window over: walkable again.
        CHECK(!IsFocusPointAvoided(MAP, -2722.0f, 13775.0f, NOW + kUnreachableFocusMs + 1, spots, kUnreachableFocusSpots));
    }

    {
        // More failures than slots keep the newest marks.
        UnreachableFocusSpot spots[kUnreachableFocusSpots] = {};
        for (std::uint32_t i = 0; i <= kUnreachableFocusSpots; ++i)
            AddUnreachableFocusSpot(spots, kUnreachableFocusSpots, MAP, 100.0f * i, 0.0f, NOW + i);
        CHECK(!IsFocusPointAvoided(MAP, 0.0f, 0.0f, NOW + 100, spots, kUnreachableFocusSpots));
        for (std::uint32_t i = 1; i <= kUnreachableFocusSpots; ++i)
            CHECK(IsFocusPointAvoided(MAP, 100.0f * i, 0.0f, NOW + 100, spots, kUnreachableFocusSpots));
    }

    std::cout << "All order focus policy tests passed.\n";
    return 0;
}
