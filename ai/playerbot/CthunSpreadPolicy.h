#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace ai
{
// Coordinates from Turtle boss_cthun.cpp, eyeTentaclePositions. These are
// eruption centers, NOT player markers. Radii below are search bounds; every
// chosen position still needs floor, path, range and LOS validation.
struct CthunPoint { float x = 0, y = 0; };
constexpr CthunPoint kCthunCenter{-8577.3f, 1986.9f};
constexpr std::array<CthunPoint, 8> kCthunEyes{{
    {-8547.269531f,1986.939941f}, {-8556.047852f,2008.144653f},
    {-8577.246094f,2016.939941f}, {-8598.457031f,2008.178467f},
    {-8607.269531f,1986.987671f}, {-8598.525391f,1965.769043f},
    {-8577.340820f,1956.940063f}, {-8556.115234f,1965.667725f}}};
constexpr float kCthunSpacing = 13.0f;
constexpr float kCthunPi = 3.14159265358979323846f;
inline float CthunDistance(CthunPoint a, CthunPoint b)
{ return std::hypot(a.x-b.x, a.y-b.y); }
inline bool CthunSurface(unsigned map, float x, float y, float z)
{
    return map == 531 && std::isfinite(z) && z > 90 && z < 110 &&
        CthunDistance({x,y}, kCthunCenter) < 95;
}
inline bool CthunSpawnClear(CthunPoint p, float clearance = 12.0f)
{
    for (auto eye : kCthunEyes) if (CthunDistance(p, eye) < clearance) return false;
    return true;
}
inline float CthunSegmentDistance(CthunPoint a, CthunPoint b, CthunPoint p)
{
    float const dx=b.x-a.x, dy=b.y-a.y, n=dx*dx+dy*dy;
    float const t=n > .001f ? std::max(0.0f,std::min(1.0f,((p.x-a.x)*dx+(p.y-a.y)*dy)/n)) : 0;
    return CthunDistance({a.x+t*dx,a.y+t*dy},p);
}
// Allow escape from an existing overlap, never a route that first deepens it.
inline bool CthunSegmentClear(CthunPoint a, CthunPoint b, CthunPoint obstacle, float radius)
{
    float const start=CthunDistance(a,obstacle);
    return CthunSegmentDistance(a,b,obstacle) + .01f >= std::min(radius,start) &&
        (start >= radius || CthunDistance(b,obstacle) > start + .01f);
}
struct CthunDuty
{
    bool healer = false;
    bool clawTank = false;
    bool hasOtherTarget = false;
    bool lineOfSight = true;
    bool meleeContact = true;
    float distance = 0;
    float range = 0;
};
inline bool CthunDutyCompatible(CthunDuty const& duty)
{
    if(!duty.hasOtherTarget) return true;
    if(duty.healer) return duty.lineOfSight && duty.distance < duty.range-2;
    if(duty.clawTank) return duty.lineOfSight && duty.meleeContact;
    // Eye/body DPS waits at its sector position when melee space is full.
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
// Bounded deterministic sector candidates. No hardcoded player standing ring.
inline std::vector<CthunPoint> CthunCandidates(unsigned sector, unsigned rank)
{
    struct Candidate { CthunPoint point; float score; };
    std::vector<Candidate> candidates;
    for (float radius=12; radius<=58; radius+=2)
        for (int degrees=-18; degrees<=18; degrees+=6)
        {
            float const angle=(sector*45.0f+degrees)*kCthunPi/180;
            CthunPoint const p{kCthunCenter.x+radius*std::cos(angle),kCthunCenter.y+radius*std::sin(angle)};
            if (!CthunSpawnClear(p)) continue;
            float const preferred=18.0f+9.0f*rank;
            candidates.push_back({p,(radius-preferred)*(radius-preferred)+std::abs(degrees+18)*.4f});
        }
    std::stable_sort(candidates.begin(),candidates.end(),[](Candidate const& a,Candidate const& b){return a.score<b.score;});
    std::vector<CthunPoint> result;
    for (auto const& c:candidates) result.push_back(c.point);
    return result;
}
// Runtime supplies terrain and duty constraints. Reserve humans/unassigned
// members at their real positions. Include all five roster slots even when a
// member is dead/downstairs, so deaths/swallowing never renumber the raid.
template<class Eligible>
std::vector<CthunPlacement> CthunPlan(std::vector<CthunSlot> slots, Eligible eligible,
    std::vector<CthunPlacement> const& previous = {})
{
    std::stable_sort(slots.begin(),slots.end(),[](CthunSlot const& a,CthunSlot const& b){
        if(a.sector!=b.sector) return a.sector<b.sector;
        if(a.role!=b.role) return a.role<b.role;
        return a.guid<b.guid;
    });
    std::array<unsigned,8> counts{};
    std::vector<std::pair<CthunSlot,unsigned>> ranked;
    std::vector<std::pair<CthunPoint,float>> reserved;
    std::vector<CthunPlacement> plan;
    for (auto const& slot:slots)
    {
        if(slot.sector>=8) continue;
        unsigned rank=counts[slot.sector]++;
        if (!slot.movable) { reserved.push_back({slot.here,slot.radius}); plan.push_back({slot.guid,slot.here,false,slot.radius}); }
        else if(rank<5) ranked.push_back({slot,rank});
    }
    // Inner ranks first across ALL sectors, not an entire quadrant first.
    std::stable_sort(ranked.begin(),ranked.end(),[](auto const& a,auto const& b){return a.second<b.second;});
    for(auto const& item:ranked)
    {
        auto const& slot=item.first;
        bool found=false;
        auto candidates=CthunCandidates(slot.sector,item.second);
        for(auto const& old:previous) if(old.guid==slot.guid && old.assigned)
        {
            float angle=std::atan2(old.point.y-kCthunCenter.y,old.point.x-kCthunCenter.x);
            float delta=std::remainder(angle-slot.sector*kCthunPi/4,2*kCthunPi);
            if(std::abs(delta)<kCthunPi/8 && CthunSpawnClear(old.point))
                candidates.insert(candidates.begin(),old.point);
        }
        for(auto const& p:candidates)
        {
            bool clear=true;
            for(auto const& q:reserved) if(CthunDistance(p,q.first)<std::max(kCthunSpacing,11.75f+slot.radius+q.second)) {clear=false;break;}
            if(!clear || !eligible(slot,p)) continue;
            plan.push_back({slot.guid,p,true,slot.radius}); reserved.push_back({p,slot.radius}); found=true; break;
        }
        if(!found) {plan.push_back({slot.guid,slot.here,false,slot.radius});reserved.push_back({slot.here,slot.radius});}
    }
    // Failed slots remain at real coordinates. Propagate any resulting
    // conflicts instead of reserving a destination nobody can reach.
    for(unsigned pass=0;pass<plan.size();++pass)
    {
        bool changed=false;
        for(auto& p:plan) if(p.assigned)
            for(auto const& q:plan) if(p.guid!=q.guid && CthunDistance(p.point,q.point)<std::max(kCthunSpacing,11.75f+p.radius+q.radius))
            {
                p.assigned=false;
                for(auto const& slot:slots) if(slot.guid==p.guid) {p.point=slot.here;break;}
                changed=true;break;
            }
        if(!changed) break;
    }
    return plan;
}
}

namespace ai
{
// Visibility graph around the eight known rupture circles. The graph is only
// a route proposal: the runtime validates every navmesh segment before moving.
struct CthunObstacle { CthunPoint point; float radius; };
inline std::vector<CthunPoint> CthunRoute(CthunPoint start,CthunPoint end,
    std::vector<CthunObstacle> obstacles = {})
{
    auto clear=[&](CthunPoint a,CthunPoint b) {
        for(auto eye:kCthunEyes) if(!CthunSegmentClear(a,b,eye,10.75f)) return false;
        for(auto const& obstacle:obstacles)
            if(CthunDistance(start,obstacle.point)>=obstacle.radius &&
                CthunSegmentDistance(a,b,obstacle.point)<obstacle.radius) return false;
        return true;
    };
    if(clear(start,end)) return {start,end};
    std::vector<CthunPoint> nodes{start,end};
    for(auto eye:kCthunEyes) for(unsigned i=0;i<16;++i)
    {
        float a=i*kCthunPi/8;
        CthunPoint p{eye.x+11.25f*std::cos(a),eye.y+11.25f*std::sin(a)};
        if(CthunSpawnClear(p,10.75f)) nodes.push_back(p);
    }
    // Bound dynamic detour nodes to eight nearby blockers. All surface
    // players still participate in edge validation, including humans.
    std::stable_sort(obstacles.begin(),obstacles.end(),[&](auto const& a,auto const& b) {
        return CthunDistance(start,a.point)<CthunDistance(start,b.point);
    });
    unsigned blockers=0;
    for(auto const& obstacle:obstacles)
    {
        if(CthunDistance(start,obstacle.point)<obstacle.radius) continue; // existing entry clump
        if(blockers++==8) break;
        float radius=obstacle.radius/ std::cos(kCthunPi/8)+.25f;
        for(unsigned i=0;i<8;++i)
        {
            float angle=i*kCthunPi/4;
            CthunPoint p{obstacle.point.x+radius*std::cos(angle),obstacle.point.y+radius*std::sin(angle)};
            if(CthunDistance(p,kCthunCenter)<94 && CthunSpawnClear(p,10.75f)) nodes.push_back(p);
        }
    }
    std::vector<float> distance(nodes.size(),std::numeric_limits<float>::max());
    std::vector<int> parent(nodes.size(),-1);
    std::vector<bool> done(nodes.size(),false);
    distance[0]=0;
    for(unsigned step=0;step<nodes.size();++step)
    {
        int current=-1;
        for(unsigned i=0;i<nodes.size();++i)
            if(!done[i] && (current<0 || distance[i]<distance[current])) current=i;
        if(current<0 || distance[current]==std::numeric_limits<float>::max()) break;
        if(current==1) break;
        done[current]=true;
        for(unsigned i=0;i<nodes.size();++i)
        {
            float next=distance[current]+CthunDistance(nodes[current],nodes[i]);
            if(!done[i] && next<distance[i] && clear(nodes[current],nodes[i]))
            {distance[i]=next;parent[i]=current;}
        }
    }
    if(parent[1]<0) return {};
    std::vector<CthunPoint> result;
    for(int i=1;i>=0;i=parent[i]) result.push_back(nodes[i]);
    std::reverse(result.begin(),result.end());
    return result;
}
}
