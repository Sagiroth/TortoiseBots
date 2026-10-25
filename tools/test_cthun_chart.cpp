#include "../ai/playerbot/CthunSpreadPolicy.h"
#include <cassert>
#include <iostream>
using namespace ai;
int main()
{
    std::vector<CthunSlot> roster;
    for(unsigned group=0;group<8;++group)
        for(unsigned role=0;role<5;++role)
            roster.push_back({group*5+role+1,group,role<2?0u:role<4?1u:2u,{-8650,2020},true});
    auto plan=CthunPlan(roster,[](auto const&,auto){return true;});
    // Regression: batch 11j searches unrelated radii/angles; cannot reproduce
    // the owner's 1 red / 2 green / 2 yellow fixed chart.
    const float expected[5]={17.5f,40,40,53.5f,53.5f};
    float minimum=100,spawn=100;
    for(auto const& slot:plan)
    {
        unsigned rank=(slot.guid-1)%5;
        assert(slot.assigned);
        assert(std::abs(CthunDistance(slot.point,kCthunCenter)-expected[rank])<.01f);
        for(auto other:plan) if(other.guid!=slot.guid) minimum=std::min(minimum,CthunDistance(slot.point,other.point));
        for(auto eye:kCthunEyes) spawn=std::min(spawn,CthunDistance(slot.point,eye));
    }
    assert(minimum>13);
    assert(spawn>12);
    std::cout<<"40 slots: min player center "<<minimum<<", min birth center "<<spawn<<"\n";
#ifdef CTHUN_CHART_EXTENDED
    assert(minimum-2*.9747f-2*kCthunArrival>10); // two normal Tauren at worst arrival edges
    assert(spawn-.9747f-kCthunArrival>10);
    for(unsigned group=0;group<8;++group)
    {
        // Fan-out has bounded progress; no 40-player obstacle graph can seal entry.
        CthunPoint here=CthunPolar(90,157.5f*kCthunPi/180);
        auto goal=CthunChartSlot(group,0);
        for(unsigned n=0;n<20 && CthunDistance(here,goal)>.1f;++n)
            here=CthunChartStep(here,goal,group);
        assert(CthunDistance(here,goal)<.1f);
        assert(CthunTargetSector(CthunChartSlot(group,0))==group);
    }
    assert(CthunDamageRank(15589,false,false,true)==-1); // unreachable tank target
    assert(CthunDamageRank(15726,false,true,true)==1); // reachable local eye
    assert(CthunDamageRank(15726,false,true,false)==-1); // opposite sector
    assert(CthunDamageRank(15726,false,false,true)==-1); // no LOS / hunter dead zone
    assert(CthunDamageRank(15727,true,true,true)==-1); // shell
    assert(CthunDamageRank(15727,false,true,true)==5);
    assert(CthunDamageRank(15728,false,true,true)==6); // controlled claw after weakness burn
    assert(CthunDamageRank(15728,true,true,true)==4);
    assert(CthunDamageRank(15728,false,true,true,true)==0); // held tank stays
    assert(CthunDamageRank(15802,false,true,true)==-1); // stomach
    assert(CthunNearbySector(0,1)); assert(CthunNearbySector(0,2));
    assert(!CthunNearbySector(0,7));
#endif
}
