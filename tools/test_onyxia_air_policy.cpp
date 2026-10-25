#include "../ai/playerbot/OnyxiaAirPolicy.h"
#include <cassert>
#include <limits>
#include <iostream>

int main()
{
    using namespace ai;
    assert(OnyxiaAirPhase(true, true, true, false));
    assert(OnyxiaAirPhase(true, true, false, true));
    assert(!OnyxiaAirPhase(true, true, false, false));
    assert(!OnyxiaAirPhase(false, true, true, true));
    assert(!OnyxiaAirPhase(true, false, true, true));
    assert(OnyxiaSpreadFloor(-25, -215, -89));
    assert(OnyxiaSpreadFloor(5, -215, -89));
    assert(!OnyxiaSpreadFloor(5.01f, -215, -89));
    assert(!OnyxiaSpreadFloor(5, -193, -89)); // rectangle corner outside ellipse
    assert(!OnyxiaSpreadFloor(-30.127f, -254.463f, -89.440f)); // egg/whelp side
    assert(!OnyxiaSpreadFloor(-30.817f, -177.106f, -89.258f)); // opposite side
    assert(!OnyxiaSpreadFloor(-105, -215, -89)); // entrance
    assert(!OnyxiaSpreadFloor(-25, -215, -65)); // airborne point
    assert(!OnyxiaSpreadFloor(-25, -215, -200)); // invalid floor
    assert(!OnyxiaSpreadFloor(std::numeric_limits<float>::quiet_NaN(), -215, -89));
    // Every segment between legal floor vertices stays inside the convex region.
    for (int i = 0; i <= 100; ++i)
    {
        float t = i / 100.0f;
        assert(OnyxiaSpreadFloor(-50 + 45 * t, -215, -89));
    }
    std::cout << "Onyxia air policy: passed\n";
}
