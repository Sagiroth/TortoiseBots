#include "../ai/playerbot/LocustSwarmPolicy.h"
#include <cassert>
#include <limits>
int main()
{
    using namespace ai;
    assert(LocustSwarmEligible(true,true,true,true,false,false));
    assert(!LocustSwarmEligible(false,true,true,true,false,false));
    assert(!LocustSwarmEligible(true,false,true,true,false,false));
    assert(!LocustSwarmEligible(true,true,false,true,false,false));
    assert(!LocustSwarmEligible(true,true,true,false,false,false));
    assert(!LocustSwarmEligible(true,true,true,true,true,false));
    assert(!LocustSwarmEligible(true,true,true,true,false,true));
    assert(LocustSwarmNeedsRetreat(30));
    assert(LocustSwarmNeedsRetreat(32.9f));
    assert(!LocustSwarmNeedsRetreat(33));
    assert(!LocustSwarmNeedsRetreat(40));
    assert(!LocustSwarmNeedsRetreat(std::numeric_limits<float>::quiet_NaN()));
    assert(LocustSwarmBlocksAction(true,false,false));
    assert(LocustSwarmBlocksAction(false,true,false));
    assert(!LocustSwarmBlocksAction(false,false,false));
    assert(!LocustSwarmBlocksAction(true,true,true));
}
