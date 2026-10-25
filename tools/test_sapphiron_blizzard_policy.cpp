#include "../ai/playerbot/SapphironBlizzardPolicy.h"

#include <cassert>

int main()
{
    using namespace ai;

    assert(IsActiveSapphironDamageArea(16474, 28547, 0, 12000, 3.0f));
    assert(!IsActiveSapphironDamageArea(16474, 28547, 0, 0, 3.0f));
    assert(!IsActiveSapphironDamageArea(16474, 28534, 0, 12000, 3.0f));
    assert(!IsActiveSapphironDamageArea(15989, 28547, 0, 12000, 3.0f));
    assert(!IsActiveSapphironDamageArea(16474, 28547, 1, 12000, 3.0f));
    assert(!IsActiveSapphironDamageArea(16474, 28547, 0, 12000, 0.0f));

    // The damage patch remains at its cast point after the Blizzard moves.
    float const botX = 1.0f;
    float const patchX = 0.0f;
    float const movedBlizzardX = 80.0f;
    float const radiusWithMargin = 3.0f + 2.0f;
    assert(IsInsideSapphironDamageArea(botX, 0.0f, patchX, 0.0f, radiusWithMargin));
    assert(!IsInsideSapphironDamageArea(botX, 0.0f, movedBlizzardX, 0.0f, radiusWithMargin));

    SapphironBlizzardSnapshot snapshot;
    assert(snapshot.NeedsRefresh(0, 533, 1));
    snapshot.BeginRefresh(1000, 533, 1);
    snapshot.areas.push_back({16474, 1});
    for (unsigned i = 0; i < 1000; ++i)
        assert(!snapshot.NeedsRefresh(1499, 533, 1));
    assert(snapshot.NeedsRefresh(1500, 533, 1));
    assert(snapshot.NeedsRefresh(1100, 533, 2));
    assert(snapshot.NeedsRefresh(1100, 409, 1));
    snapshot.BeginRefresh(1500, 533, 1);
    assert(snapshot.areas.empty()); // refreshed discovery replaces old IDs
    snapshot.areas.push_back({16474, 2}); // newly discovered area
    assert(snapshot.areas.front().area == 2);
    snapshot.BeginRefresh(UINT32_MAX - 249, 533, 1);
    assert(!snapshot.NeedsRefresh(249, 533, 1));
    assert(snapshot.NeedsRefresh(250, 533, 1));
    // Live filtering/position checks are independent of the discovery clock.
    assert(!IsActiveSapphironDamageArea(16474, 28547, 0, 0, 3.0f));
    assert(!IsInsideSapphironDamageArea(20, 0, patchX, 0, radiusWithMargin));
}
