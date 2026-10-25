#include "../ai/playerbot/CthunSpreadPolicy.h"
#include <cassert>
#include <iostream>
using namespace ai;
int main()
{
    assert(CthunSurface(531,-8577,1987,100));
    assert(CthunSurface(531,kCthunCenter.x-94,kCthunCenter.y,100));
    assert(!CthunSurface(531,kCthunCenter.x-96,kCthunCenter.y,100));
    assert(!CthunSurface(249,-8577,1987,100));
    assert(!CthunSurface(531,-8562,2037,-96));
    std::vector<CthunSlot> slots;
    for(unsigned group=0;group<8;++group)
        for(unsigned rank=0;rank<5;++rank)
            slots.push_back({group*5+rank+1,group,rank<2?0u:rank<4?1u:2u,{-8635,1987},true});
    auto plan=CthunPlan(slots,[](auto const&,auto){return true;});
    assert(plan.size()==40);
    for(auto const& p:plan)
    {
        assert(p.assigned);
        assert(CthunSpawnClear(p.point));
        for(auto const& q:plan) if(p.guid!=q.guid)
            assert(CthunDistance(p.point,q.point)>13);
    }
    slots[0].movable=false; // dead/swallowed/manual member retains reservation
    auto stable=CthunPlan(slots,[](auto const&,auto){return true;},plan);
    for(unsigned i=0;i<plan.size();++i)
        assert(CthunDistance(stable[i].point,plan[i].point)<.01f);
    assert(!stable[0].assigned);
    auto blocked=CthunPlan(slots,[](auto const&,auto){return false;});
    for(auto const& p:blocked) assert(!p.assigned);
    std::cout << "Cthun: fixed 40 slots, scope, spacing, stable reservations PASS\n";
}
