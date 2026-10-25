#include "../ai/playerbot/CthunSpreadPolicy.h"
#include <cassert>
#include <iostream>
using namespace ai;
int main()
{
    assert(CthunSurface(531,-8577,1987,100));
    assert(CthunSurface(531,kCthunCenter.x-94,kCthunCenter.y,100)); // entry before ordinary 60yd cache
    assert(!CthunSurface(531,kCthunCenter.x-96,kCthunCenter.y,100));
    assert(!CthunSurface(249,-8577,1987,100));
    assert(!CthunSurface(531,-8562,2037,-96));
    assert(!CthunSurface(531,-8700,1987,100));
    CthunDuty duty;
    duty.hasOtherTarget=true;duty.distance=50;duty.range=40;
    assert(CthunDutyCompatible(duty)); // extra melee waits, never stacks
    duty.healer=true;
    assert(!CthunDutyCompatible(duty)); // preserve heal range
    duty.distance=30;assert(CthunDutyCompatible(duty));
    duty.lineOfSight=false;assert(!CthunDutyCompatible(duty));
    duty.healer=false;duty.clawTank=true;duty.lineOfSight=true;
    duty.meleeContact=false;assert(!CthunDutyCompatible(duty));
    duty.meleeContact=true;assert(CthunDutyCompatible(duty));
    std::vector<CthunSlot> slots;
    for(unsigned sector=0;sector<8;++sector)
        for(unsigned rank=0;rank<5;++rank)
            slots.push_back({sector*5+rank+1,sector,rank<2?0u:rank<4?1u:2u,{-8635,1987},true});
    auto plan=CthunPlan(slots,[](auto const&,auto){return true;});
    assert(plan.size()==40);
    for(auto const& p:plan)
    {
        assert(p.assigned);
        assert(CthunSpawnClear(p.point));
        // Every sector receives an inner slot, not just the first sectors
        // in iteration order. Extra melee slots may be outside boss reach.
        if((p.guid-1)%5==0) assert(CthunDistance(p.point,kCthunCenter)<=20.01f);
        for(auto const& q:plan) if(p.guid!=q.guid)
            assert(CthunDistance(p.point,q.point)>=kCthunSpacing);
        auto route=CthunRoute({-8635,1987},p.point);
        assert(route.size()>=2);
        for(unsigned i=1;i<route.size();++i)
            for(auto eye:kCthunEyes) assert(CthunSegmentClear(route[i-1],route[i],eye,10.75f));
    }
    auto stable=CthunPlan(slots,[](auto const&,auto){return true;},plan);
    for(unsigned i=0;i<plan.size();++i)
        assert(CthunDistance(stable[i].point,plan[i].point)<.01f);
    // Actual coordinates (not an ideal circle): a safe endpoint across an
    // eruption is insufficient; the returned route must detour around it.
    auto eye=kCthunEyes[0];
    CthunPoint a{eye.x-14,eye.y},b{eye.x+14,eye.y};
    assert(CthunSegmentDistance(a,b,eye)<10);
    auto detour=CthunRoute(a,b);assert(detour.size()>2);
    // A settled player blocks the direct route, not every route. The
    // planner must detour instead of leaving an entrant in the doorway.
    CthunPoint west{kCthunCenter.x-56,kCthunCenter.y-12};
    CthunPoint north{kCthunCenter.x-56,kCthunCenter.y+12};
    CthunObstacle member{{kCthunCenter.x-56,kCthunCenter.y},11.5f};
    auto around=CthunRoute(west,north,{member});assert(around.size()>2);
    for(unsigned i=1;i<around.size();++i)
        assert(CthunSegmentDistance(around[i-1],around[i],member.point)>=member.radius);
    // Spawn overlap can escape, but cannot first move deeper into the circle.
    assert(CthunSegmentClear({eye.x+5,eye.y},{eye.x+8,eye.y},eye,12));
    assert(!CthunSegmentClear({eye.x+5,eye.y},{eye.x-15,eye.y},eye,12));
    // Humans/disabled bots occupy real positions and never receive orders.
    slots[0].movable=false;slots[0].here=plan[1].point;
    auto occupied=CthunPlan(slots,[](auto const&,auto){return true;});
    for(auto const& p:occupied)
    {
        if(p.guid==slots[0].guid) assert(!p.assigned);
        if(p.assigned) assert(CthunDistance(p.point,slots[0].here)>=kCthunSpacing);
    }
    // A floor/range/LOS failure must not produce a pretend valid destination.
    auto blocked=CthunPlan(slots,[](auto const&,auto){return false;});
    for(auto const& p:blocked) assert(!p.assigned);
    std::cout << "Cthun: 40 slots, cross-sector spacing, spawn routes, entry, scope and constraints PASS\n";
}
