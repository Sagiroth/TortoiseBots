#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ai
{
// Coordinates from Turtle boss_cthun.cpp, eyeTentaclePositions. These are
// eruption centers, NOT player markers. Fixed player slots are below;
// the runtime still checks floor and complete navmesh paths.
struct CthunPoint { float x = 0, y = 0; };
constexpr CthunPoint kCthunCenter{-8577.3f, 1986.9f};
constexpr std::array<CthunPoint, 8> kCthunEyes{{
    {-8547.269531f,1986.939941f}, {-8556.047852f,2008.144653f},
    {-8577.246094f,2016.939941f}, {-8598.457031f,2008.178467f},
    {-8607.269531f,1986.987671f}, {-8598.525391f,1965.769043f},
    {-8577.340820f,1956.940063f}, {-8556.115234f,1965.667725f}}};
constexpr float kCthunSpacing = 13.0f;
// Tauren male bounding radius is .9747 in the native display addon.
// With two .25-yard arrival errors, the tightest pair stays over 10 yards.
constexpr float kCthunArrival = .25f;
constexpr float kCthunPi = 3.14159265358979323846f;
inline float CthunDistance(CthunPoint a, CthunPoint b)
{ return std::hypot(a.x-b.x, a.y-b.y); }
inline bool CthunSurface(unsigned map, float x, float y, float z)
{
    return map == 531 && std::isfinite(z) && z > 90 && z < 110 &&
        CthunDistance({x,y}, kCthunCenter) < 95;
}
// The western approach/ramp is part of entry only after engagement or an
// explicit Eye pull. Keep the stomach and the rest of AQ40 out of scope.
inline bool CthunApproach(unsigned map, float x, float y, float z)
{
    return CthunSurface(map,x,y,z) || (map==531 && std::isfinite(z) &&
        z>90 && z<125 && x>-8705 && x<-8640 && y>1935 && y<2045);
}
// Static-vmap LOS blocked at this waiting point; ground navmesh checked.
// Eye Beam ignores LOS: this prevents premature aggro, not combat damage.
constexpr CthunPoint kCthunWaiting{-8675.0f,1968.0f};
constexpr float kCthunWaitingZ=108.90f;
constexpr CthunPoint kCthunEntry{-8640.0f,1975.0f};
inline bool CthunSpawnClear(CthunPoint p, float clearance = 12.0f)
{
    for (auto eye : kCthunEyes) if (CthunDistance(p, eye) < clearance) return false;
    return true;
}
struct CthunSlot
{
    uint32_t guid = 0;
    unsigned sector = 0;
    unsigned role = 0; // tank/melee 0, ranged 1, healer 2
    CthunPoint here;
    bool movable = false;
    float radius = .5f;
};
struct CthunPlacement { uint32_t guid; CthunPoint point; bool assigned; float radius = .5f; };
// Chart order: left 1/3/5/7, right 2/4/6/8, entrance below.
// Preserve the deployed chart orientation: chart-up is -22.5 degrees.
// Object 180745 is Grasp of C'Thun, not a doorway; the approach route
// below uses the actual western ramp, independently of chart orientation.
inline float CthunSectorAngle(unsigned sector)
{
    constexpr float degrees[8]={22.5f,-22.5f,67.5f,-67.5f,112.5f,-112.5f,157.5f,-157.5f};
    return degrees[sector%8]*kCthunPi/180;
}
inline CthunPoint CthunPolar(float radius,float angle)
{ return {kCthunCenter.x+radius*std::cos(angle),kCthunCenter.y+radius*std::sin(angle)}; }
// Fixed 1 red / 2 green / 2 yellow chart. Literal chart scaling to the
// 30-yard birth ring cannot fit 40 players. Expand the middle/outer rings
// beyond that ring; retain stagger and subgroup order. All 40 endpoints
// checked against map 531's ground navmesh. Group 6's second yellow needs
// a 3-degree adjustment to clear an obstacle, not a runtime slot search.
inline CthunPoint CthunChartSlot(unsigned sector,unsigned rank)
{
    constexpr float radii[5]={17.5f,40,40,53.5f,53.5f};
    constexpr float offsets[5]={0,-11.25f,11.25f,-14,14};
    float offset=offsets[rank%5]+(sector==5 && rank==4 ? 3 : 0);
    return CthunPolar(radii[rank%5],CthunSectorAngle(sector)+offset*kCthunPi/180);
}
// Roster slots remain reserved for humans, disabled bots, dead and swallowed
// members. A failed route or a changing heal target never renumbers peers.
template<class Eligible>
std::vector<CthunPlacement> CthunPlan(std::vector<CthunSlot> slots,Eligible eligible,
    std::vector<CthunPlacement> const& = {})
{
    std::stable_sort(slots.begin(),slots.end(),[](auto const& a,auto const& b) {
        if(a.sector!=b.sector) return a.sector<b.sector;
        if(a.role!=b.role) return a.role<b.role;
        return a.guid<b.guid;
    });
    std::array<unsigned,8> counts{};
    std::vector<CthunPlacement> plan;
    for(auto const& slot:slots)
    {
        if(slot.sector>=8) continue;
        unsigned rank=counts[slot.sector]++;
        if(rank>=5) continue;
        auto point=CthunChartSlot(slot.sector,rank);
        plan.push_back({slot.guid,point,slot.movable && eligible(slot,point),slot.radius});
    }
    return plan;
}
// Fixed entry lane, not a visibility graph through 40 overlapping player
// keepouts. Fan out around the 43-yard ring before entering the own sector.
// Transit cannot guarantee final spacing (the doorway starts clumped).
inline CthunPoint CthunChartStep(CthunPoint here,CthunPoint destination,unsigned sector)
{
    float angle=std::atan2(here.y-kCthunCenter.y,here.x-kCthunCenter.x);
    float delta=std::remainder(CthunSectorAngle(sector)-angle,2*kCthunPi);
    if(std::abs(delta)<kCthunPi/8) return destination;
    if(CthunDistance(here,kCthunCenter)>47) return CthunPolar(43,angle);
    float step=std::max(-kCthunPi/8,std::min(kCthunPi/8,delta));
    return CthunPolar(43,angle+step);
}
// Join the chart lanes through the actual ramp mouth, not via a point on
// the upper western ledge. Runtime follows a complete navmesh path to it.
inline CthunPoint CthunEntryStep(CthunPoint here, float z, CthunPoint destination,unsigned sector)
{
    if(here.x<-8645 || z>105) return kCthunEntry;
    return CthunChartStep(here,destination,sector);
}
inline unsigned CthunTargetSector(CthunPoint point)
{
    float angle=std::atan2(point.y-kCthunCenter.y,point.x-kCthunCenter.x);
    unsigned result=0;float best=10;
    for(unsigned sector=0;sector<8;++sector)
    {
        float delta=std::abs(std::remainder(angle-CthunSectorAngle(sector),2*kCthunPi));
        if(delta<best) {result=sector;best=delta;}
    }
    return result;
}
inline bool CthunNearbySector(unsigned own,unsigned other)
{ return std::abs(std::remainder(CthunSectorAngle(own)-CthunSectorAngle(other),2*kCthunPi))<kCthunPi/4+.01f; }
// Normal damage priority; existing tank/control actions are not replaced.
inline int CthunDamageRank(unsigned entry,bool shelled,bool feasible,bool local,bool heldClaw=false)
{
    if(!feasible) return -1;
    if(entry==15728 && heldClaw) return 0;
    if(entry==15726 && local) return 1;
    if(entry==15334 && local) return 2;
    if(entry==15725 && local) return 3;
    if(entry==15728 && local) return shelled ? 4 : 6;
    if(entry==15589 || (entry==15727 && !shelled)) return 5;
    return -1; // includes Flesh Tentacles below the room and shelled body
}
}
