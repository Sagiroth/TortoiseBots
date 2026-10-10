#include "../ai/playerbot/RearFlankPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::Dist2Sq;
using ai::FlankHeadingDistance;
using ai::FlankOffset;
using ai::kFlankMaxAngle;
using ai::kFlankMinAngle;
using ai::NeedsRearFlank;

int main()
{
    std::cout << "Starting TortoiseBots rear-flank policy tests...\n";

    // Dead ahead of the mob: flank. Directly behind: hold (set-behind
    // owns that point — the tail clause is deliberately dropped so the
    // flank row cannot ping-pong against set-behind's destination).
    // Off to the side past 90 degrees: hold.
    CHECK(NeedsRearFlank(0.0f, 3.1415926536f));
    CHECK(NeedsRearFlank(1.0f, 2.5f));
    CHECK(!NeedsRearFlank(1.8f, 0.3f));
    CHECK(!NeedsRearFlank(3.1415926536f, 0.0f));
    std::cout << "  [PASS] front arc fires, flank and rear hold\n";

    // Boundary: exactly 90 degrees off the facing is safe (donor uses a
    // strict less-than on the front arc).
    CHECK(!NeedsRearFlank(kFlankMinAngle, 1.0f));
    CHECK(NeedsRearFlank(kFlankMinAngle - 0.01f, 1.0f));
    std::cout << "  [PASS] 90-degree boundary is exclusive\n";

    // Tail input is ignored: even far off the tail axis, a bot outside the
    // front arc holds (documents the dropped donor tail clause).
    CHECK(!NeedsRearFlank(2.0f, kFlankMaxAngle + 0.01f));
    CHECK(!NeedsRearFlank(2.0f, kFlankMaxAngle - 0.01f));
    std::cout << "  [PASS] tail input ignored (front-arc-only)\n";

    // Flank offsets mirror about the facing axis at the flank radius.
    {
        float lx, ly, rx, ry;
        FlankOffset(0.0f, 0.0f, 0.0f, 1.8f, 4.0f, lx, ly);
        FlankOffset(0.0f, 0.0f, 0.0f, -1.8f, 4.0f, rx, ry);
        // cos(1.8) < 0 so both flank x land behind the target; y mirrors.
        CHECK(lx < 0.0f && rx < 0.0f);
        CHECK(ly > 0.0f && ry < 0.0f);
        CHECK(Dist2Sq(lx, ly, 0.0f, 0.0f) > 15.9f && Dist2Sq(lx, ly, 0.0f, 0.0f) < 16.1f);
        CHECK(Dist2Sq(rx, ry, 0.0f, 0.0f) > 15.9f && Dist2Sq(rx, ry, 0.0f, 0.0f) < 16.1f);
    }
    std::cout << "  [PASS] flank offsets mirror at the flank radius\n";

    // Nearer-side pick helper: squared distance orders the same as true
    // distance.
    CHECK(Dist2Sq(0.0f, 0.0f, 3.0f, 4.0f) == 25.0f);
    CHECK(Dist2Sq(0.0f, 0.0f, 1.0f, 1.0f) < Dist2Sq(0.0f, 0.0f, 3.0f, 4.0f));
    std::cout << "  [PASS] nearer-side comparison\n";

    // Heading distance wraps a full turn.
    CHECK(FlankHeadingDistance(0.0f, 6.2831853072f) < 0.001f);
    CHECK(FlankHeadingDistance(0.0f, 3.1415926536f) > 3.14f);
    std::cout << "  [PASS] heading distance wraps\n";

    std::cout << "All rear-flank policy tests passed.\n";
    return 0;
}
