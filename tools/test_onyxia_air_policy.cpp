#include "../ai/playerbot/OnyxiaAirPolicy.h"
#include <cassert>
#include <limits>
#include <iostream>

int main()
{
    using namespace ai;
    assert(OnyxiaWhelpRole(true, false, false));
    assert(OnyxiaWhelpRole(false, false, false));
    assert(!OnyxiaWhelpRole(false, true, false));
    assert(!OnyxiaWhelpRole(false, false, true));
    assert(OnyxiaWhelpScore(true, true, false, 40) > OnyxiaWhelpScore(true, false, true, 1));
    assert(OnyxiaWhelpScore(false, true, false, 40) < OnyxiaWhelpScore(false, false, true, 1));
    assert(OnyxiaWhelpScore(false, false, true, 15) > OnyxiaWhelpScore(false, false, false, 10));
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
