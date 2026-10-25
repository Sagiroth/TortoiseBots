#include "../ai/playerbot/KelThuzadFissurePolicy.h"
#include <cassert>
#include <limits>
using namespace ai;
int main()
{
    KtFissureCircle hazard{0,0,7};
    assert(KT_FISSURE_CLEARANCE > 5);
    assert(KtFissureSegmentSafe({0,0},{8,0},hazard));
    assert(KtFissureSegmentSafe({3,0},{8,0},hazard));
    assert(!KtFissureSegmentSafe({3,0},{-8,0},hazard));
    assert(!KtFissureSegmentSafe({-8,0},{8,0},hazard));
    assert(KtFissureSegmentSafe({-8,8},{8,8},hazard));
    assert(!KtFissureSegmentSafe({0,0},{std::numeric_limits<float>::quiet_NaN(),0},hazard));
    KtFissureSnapshot snapshot;
    assert(snapshot.NeedsRefresh(1,533,1));
    snapshot={true,533,1,100,{42}};
    assert(!snapshot.NeedsRefresh(599,533,1));
    assert(snapshot.NeedsRefresh(600,533,1));
    assert(snapshot.NeedsRefresh(101,533,2));
    assert(snapshot.NeedsRefresh(101,469,1));
    snapshot.refreshedAt=UINT32_MAX-100;
    assert(!snapshot.NeedsRefresh(100,533,1));
    assert(snapshot.NeedsRefresh(500,533,1));
}
