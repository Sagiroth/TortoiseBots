#include "../ai/playerbot/OnyxiaTankPositionPolicy.h"
#include <cassert>
#include <limits>
using namespace ai;
int main()
{
    OnyxiaTankPoint start{-55, -215, -85};
    std::vector<OnyxiaTankPoint> route{start, {-25,-215,-88}, kOnyxiaTankSpot};
    assert(OnyxiaTankRouteSafe(start, route));
    OnyxiaTankPoint knockedBack{30, -217, -85};
    assert(OnyxiaTankRouteSafe(knockedBack, {knockedBack, kOnyxiaTankSpot}));
    assert(!OnyxiaTankRouteSafe(start, {}));
    assert(!OnyxiaTankRouteSafe(start, {start}));
    assert(!OnyxiaTankRouteSafe(start, {start, {-15,-215,-86}})); // incomplete
    assert(!OnyxiaTankRouteSafe(start, {{-25,-215,-88}, kOnyxiaTankSpot})); // disconnected
    for (float caveY : {-254.463f, -177.106f})
    {
        auto detour = route;
        detour[1] = {-30, caveY, -89};
        assert(!OnyxiaTankRouteSafe(start, detour)); // safe endpoint is insufficient
        assert(!OnyxiaTankCorridor(detour[1]));
    }
    auto invalid = route;
    invalid[1].z = -65; assert(!OnyxiaTankRouteSafe(start, invalid)); // flight height
    invalid[1] = {std::numeric_limits<float>::quiet_NaN(), -215, -85};
    assert(!OnyxiaTankRouteSafe(start, invalid));
    assert(!OnyxiaTankRouteSafe({-30,-254,-89}, route)); // cannot rescue from a cave
    auto loop = route;
    loop.insert(loop.end()-1, {start, {-25,-215,-88}, start, {-25,-215,-88}});
    assert(!OnyxiaTankRouteSafe(start, loop));
}
