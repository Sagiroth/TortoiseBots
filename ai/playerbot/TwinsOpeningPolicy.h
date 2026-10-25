#pragma once
#include <cmath>

namespace ai
{
struct TwinsPoint { float x=0, y=0, z=-104.2f; };
// Opposite spawn-side platforms, verified against the map-531 mesh.
// Side labels are encounter left/right from the entrance, not map compass axes.
constexpr TwinsPoint kTwinsWest{-9019.14f,1180.07f,-104.17f};
constexpr TwinsPoint kTwinsEast{-8874.06f,1204.74f,-104.17f};
constexpr float kTwinsMidX=-8946.6f;
inline float TwinsDistance(TwinsPoint a,TwinsPoint b) { return std::hypot(a.x-b.x,a.y-b.y); }
inline bool TwinsRoom(unsigned map,TwinsPoint p)
{ return map==531 && p.x>-9070 && p.x<-8810 && p.y>1125 && p.y<1370 && p.z>-120 && p.z<-85; }
inline int TwinsSide(bool west,bool east) { return west==east ? -1 : east ? 1 : 0; }
inline bool TwinsOnSide(TwinsPoint p,int side)
{ return side==0 ? p.x<kTwinsMidX-12 : side==1 && p.x>kTwinsMidX+12; }
inline TwinsPoint TwinsOutward(TwinsPoint p,int side,float distance)
{
    float sign=side==0 ? -1.f : 1.f;
    return {p.x+sign*.98585f*distance,p.y+sign*.16765f*distance,p.z};
}
inline TwinsPoint TwinsTankCamp(int side)
{ return TwinsOutward(side==0 ? kTwinsWest : kTwinsEast,side,5.f); }
inline bool TwinsSeparated(TwinsPoint a,TwinsPoint b,float radii=0)
{ return TwinsDistance(a,b)>75.f+radii; }
inline unsigned TwinsDamageEntry(bool physical,bool spell)
{ return physical==spell ? 0 : physical ? 15275 : 15276; }
inline bool TwinsSafeStep(TwinsPoint start,TwinsPoint end,TwinsPoint caster,float radius)
{
    // An already trapped player may escape radially; a safe player may not
    // start a route through the burst circle. Called on densely sampled paths.
    float before=TwinsDistance(start,caster),after=TwinsDistance(end,caster);
    return before<radius ? after+0.05f>=before : after>=radius;
}
}
