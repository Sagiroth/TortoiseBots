#include "../ai/playerbot/DragonFlankPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static float Normalized(float angle)
{
    const float kTwoPi = 6.2831853072f;
    while (angle < 0.0f)
        angle += kTwoPi;
    while (angle >= kTwoPi)
        angle -= kTwoPi;
    return angle;
}

static bool Near(float a, float b)
{
    float const diff = Normalized(b - a);
    return diff < 0.001f || 6.2831853072f - diff < 0.001f;
}

int main()
{
    std::cout << "Starting TortoiseBots dragon flank policy tests...\n";

    // The candidates are the two sides of the facing axis and nothing else:
    // the near side first, the far side second, and never the front or the
    // tail (the cones the step exists to leave).
    DragonFlankPoint points[kDragonFlankPointCount];
    CHECK(kDragonFlankPointCount == 2);
    DragonFlankPoints(0.0f, 1.0f, points);
    CHECK(points[0].nearSide && !points[1].nearSide);
    CHECK(Near(points[0].angle, 1.5707963268f));
    CHECK(Near(points[1].angle, -1.5707963268f));
    CHECK(!Near(points[0].angle, 0.0f) && !Near(points[1].angle, 0.0f));
    CHECK(!Near(points[0].angle, 3.1415926536f) && !Near(points[1].angle, 3.1415926536f));
    std::cout << "  [PASS] the candidates are the near and far sides\n";

    // A bot standing on the other side picks that side first.
    DragonFlankPoints(0.0f, -1.0f, points);
    CHECK(Near(points[0].angle, -1.5707963268f));
    CHECK(Near(points[1].angle, 1.5707963268f));
    std::cout << "  [PASS] the nearer side comes first\n";

    // Production orientations are in [0, 2PI), so the side is the sign
    // of the signed delta folded into [-PI, PI]. A bot just clockwise of a
    // north-facing dragon (2PI - 1, i.e. about -1 rad) stands on the minus
    // side, not the plus side the raw subtraction reports.
    DragonFlankPoints(0.0f, 6.2831853072f - 1.0f, points);
    CHECK(points[0].nearSide && !points[1].nearSide);
    CHECK(Near(points[0].angle, -1.5707963268f));
    CHECK(Near(points[1].angle, 1.5707963268f));

    // The same seam across north: facing 2PI - 0.1 and a bot at 0.1 is
    // 0.2 rad counter-clockwise from the facing, so the plus side is nearer.
    DragonFlankPoints(6.2831853072f - 0.1f, 0.1f, points);
    CHECK(points[0].nearSide && !points[1].nearSide);
    CHECK(Near(points[0].angle, 6.2831853072f - 0.1f + 1.5707963268f));
    CHECK(Near(points[1].angle, 6.2831853072f - 0.1f - 1.5707963268f));
    std::cout << "  [PASS] the wrapped 0/2PI seam keeps the nearer side first\n";

    // Facing anywhere: both candidates stay 90 degrees off the facing axis,
    // one on each side of it.
    for (float facing = -3.0f; facing <= 3.0f; facing += 0.5f)
    {
        for (float toBot = -3.0f; toBot <= 3.0f; toBot += 0.5f)
        {
            DragonFlankPoints(facing, toBot, points);
            CHECK(Near(points[0].angle - facing, 1.5707963268f) ||
                Near(points[0].angle - facing, -1.5707963268f));
            CHECK(Near(points[1].angle - facing, 1.5707963268f) ||
                Near(points[1].angle - facing, -1.5707963268f));
            CHECK(Near(points[1].angle - points[0].angle, 3.1415926536f));
        }
    }
    std::cout << "  [PASS] both candidates stay 90 degrees off the facing axis\n";

    // A blocked side is skipped for the other side; both blocked means no
    // point (the bot holds), never the tail or the front.
    CHECK(PickDragonFlankPoint(points, [](const DragonFlankPoint&) { return false; }) == -1);
    CHECK(PickDragonFlankPoint(points, [](const DragonFlankPoint& point) { return !point.nearSide; }) == 1);
    CHECK(PickDragonFlankPoint(points, [](const DragonFlankPoint& point) { return point.nearSide; }) == 0);
    std::cout << "  [PASS] a blocked side falls back to the other side only\n";

    // The far side is not checked at all when the near side is taken, so a
    // flank step normally costs one point's LOS and path check.
    int checked = 0;
    CHECK(PickDragonFlankPoint(points, [&checked](const DragonFlankPoint& point)
    {
        ++checked;
        return point.nearSide;
    }) == 0);
    CHECK(checked == 1);
    std::cout << "  [PASS] the far side is unchecked when the near side is taken\n";

    std::cout << "TortoiseBots dragon flank policy tests passed.\n";
    return 0;
}
