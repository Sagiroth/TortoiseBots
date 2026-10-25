#pragma once
#include "TwinsOpeningPolicy.h"
#include <cstdint>

namespace ai
{
inline int TwinsPointSide(TwinsPoint p)
{ return TwinsOnSide(p,0) ? 0 : TwinsOnSide(p,1) ? 1 : -1; }

// A clock observed from the actual pair, not from dummy spell 799. Unknown
// mid-fight activation stays conservative until a real position swap is seen.
struct TwinsTeleportState
{
    bool active=false, sawIdle=false, knownCycle=false, handoff=false, validPair=false;
    bool confirming=false, opening=false, configured=false;
    uint32_t idleAt=0, cycleAt=0, sampledAt=0, confirmedAt=0, epoch=0;
    TwinsPoint physical, caster;
    bool Preparing() const
    { return active && (!knownCycle || uint32_t(sampledAt-cycleAt)>=25000); }
    bool Holding() const { return opening || Preparing() || handoff; }
    unsigned Phase() const { return !active ? 0 : opening ? 4 : handoff ? 3 : Preparing() ? 2 : 1; }
    void Observe(uint32_t now,bool engaged,TwinsPoint nextPhysical,TwinsPoint nextCaster,bool owners)
    {
        if(!engaged)
        { *this=TwinsTeleportState{};sawIdle=true;idleAt=sampledAt=now;return; }
        if(!active)
        {
            active=true;opening=true;knownCycle=sawIdle && uint32_t(now-idleAt)<2000;
            cycleAt=now;
        }
        sampledAt=now;
        int nextSide=TwinsPointSide(nextPhysical),casterSide=TwinsPointSide(nextCaster);
        bool pair=nextSide>=0 && casterSide>=0 && nextSide!=casterSide &&
            TwinsSeparated(nextPhysical,nextCaster,3.f);
        if(!pair && !handoff) opening=true; // loss of separation closes damage again
        if(opening && pair && owners) opening=false;
        if(pair && validPair && nextSide!=TwinsPointSide(physical) &&
            TwinsDistance(physical,nextPhysical)>60 && TwinsDistance(caster,nextCaster)>60)
        {
            ++epoch;cycleAt=now;knownCycle=true;handoff=true;confirming=false;
        }
        // Preserve the last complete pair across a partially observed relocation.
        if(pair){physical=nextPhysical;caster=nextCaster;validPair=true;}
        if(handoff)
        {
            if(owners && uint32_t(now-cycleAt)>=2000)
            {
                if(!confirming){confirming=true;confirmedAt=now;}
                else if(uint32_t(now-confirmedAt)>=500) handoff=false;
            }
            else confirming=false;
        }
    }
};
}
