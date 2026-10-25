#pragma once

#include <cstdint>

// Pure decision rules for Onyxia Deep Breath safe-zone dodging
// (mod-playerbots parity, raid1 batch item 7).
// Donor: mod-playerbots @ 79bd4281, src/Ai/Raid/Ony/OnyTriggers.cpp
// (OnyxiaDeepBreathTrigger: boss casting one of 8 Breath ids) +
// src/Ai/Raid/Ony/OnyActions.h:52-93 (MoveToSafeZone: nearest of 2 safe
// zones per breath direction; already-safe bots hold).
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_onyxia_breath_policy.cpp.

namespace ai
{
    // Onyxia entry + the 8 directional Deep Breath spell ids (all 1.18.1
    // verified at port time — see provenance).
    constexpr std::uint32_t kOnyxiaEntry = 10184;

    inline bool IsOnyxiaBreathSpell(std::uint32_t spellId)
    {
        switch (spellId)
        {
            case 17086: // N to S
            case 18351: // S to N
            case 18576: // E to W
            case 18609: // W to E
            case 18564: // SE to NW
            case 18584: // NW to SE
            case 18596: // SW to NE
            case 18617: // NE to SW
                return true;
            default:
                return false;
        }
    }

    // Facing-to-axis: the core faces the breath destination, so boss
    // orientation selects the lane pair. 8 compass eighths fold into 4
    // axes (opposite directions share safe zones, as in the donor table).
    inline int BreathAxisFromFacing(float facing)
    {
        // 8 compass eighths → the donor's 4 lane pairs. Cardinal breaths
        // take the cardinal axis; diagonal breaths take their diagonal.
        const float kTwoPi = 6.2831853072f;
        const float kEighth = 0.7853981634f; // PI/4
        while (facing < 0.0f)
            facing += kTwoPi;
        while (facing >= kTwoPi)
            facing -= kTwoPi;
        int eighth = (int)(facing / kEighth + 0.5f) % 8;
        switch (eighth)
        {
            case 0: // N (+X)
            case 4: // S (-X)
                return 0;
            case 2: // W (+Y)
            case 6: // E (-Y)
                return 1;
            case 3: // SW
            case 7: // NE
                return 3;
            case 5: // SE
            case 1: // NW
            default:
                return 2;
        }
    }
    // Safe-zone pair per breath direction (donor coords, Onyxia's lair map).
    // Pair index: 0 = N-S axis, 1 = E-W axis, 2 = SE-NW axis, 3 = SW-NE axis.
    // Kept for documentation/tests; production maps facing via above.
    inline int BreathAxisIndex(std::uint32_t spellId)
    {
        switch (spellId)
        {
            case 17086:
            case 18351:
                return 0;
            case 18576:
            case 18609:
                return 1;
            case 18564:
            case 18584:
                return 2;
            case 18596:
            case 18617:
                return 3;
            default:
                return -1;
        }
    }

    // Safe-zone radius: already inside means hold (donor early-out).
    constexpr float kBreathSafeZoneRadius = 5.0f;

    inline bool ShouldMoveToBreathSafeZone(bool breathCasting, bool alreadySafe)
    {
        return breathCasting && !alreadySafe;
    }
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace ai
{
struct OnyxiaBreathPoint { float x, y; };
struct OnyxiaBreathCircle { float x, y, radius; };
struct OnyxiaBreathLaunch { uint32_t spell; float x, y; };
// Turtle boss_onyxia.cpp flight endpoints, in perimeter order.
constexpr OnyxiaBreathLaunch kOnyxiaBreathLaunches[] = {
    {18617,10.2191f,-247.912f}, {18576,-31.4963f,-250.123f},
    {18564,-63.5156f,-240.096f}, {18351,-65.8444f,-213.809f},
    {18596,-58.2509f,-189.020f}, {18609,-33.5561f,-182.682f},
    {18584,6.8951f,-180.246f}, {17086,22.8763f,-217.152f}
};
inline uint32_t OnyxiaBreathDirection(float x, float y)
{
    for (auto const& launch : kOnyxiaBreathLaunches)
        if (std::hypot(x-launch.x,y-launch.y) <= 4.0f)
            return launch.spell;
    return 0;
}
inline bool OnyxiaBreathFresh(uint32_t now, uint32_t observed)
{
    return now-observed < 15000;
}
inline bool OnyxiaBreathFloor(float x, float y, float z)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || z < -93 || z > -80)
        return false;
    // Ray crossing handles the two slightly concave side endpoints correctly.
    bool inside = false;
    for (unsigned i=0,j=7;i<8;j=i++)
    {
        auto const& a=kOnyxiaBreathLaunches[i]; auto const& b=kOnyxiaBreathLaunches[j];
        if ((a.y>y)!=(b.y>y) && x<(b.x-a.x)*(y-a.y)/(b.y-a.y)+a.x)
            inside=!inside;
    }
    // Inset from both scripted whelp/egg-pit entrances.
    return inside && std::hypot(x+30.127f,y+254.463f)>=15 &&
        std::hypot(x+30.817f,y+177.106f)>=15;
}
inline float OnyxiaBreathClearance(float x,float y,std::vector<OnyxiaBreathCircle> const& circles)
{
    if (circles.empty() || !std::isfinite(x) || !std::isfinite(y))
        return -std::numeric_limits<float>::infinity();
    float clearance=std::numeric_limits<float>::infinity();
    for (auto const& c:circles)
    {
        if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.radius) || c.radius<=0)
            return -std::numeric_limits<float>::infinity();
        clearance=std::min(clearance,std::hypot(x-c.x,y-c.y)-c.radius);
    }
    return clearance;
}
// The server's vanilla monster-emote packet contains a name, not a source GUID.
// Bounds-check each field before comparing it with the observed boss/localized text.
inline bool ReadOnyxiaBossEmote(uint8_t const* bytes,size_t size,std::string& name,std::string& message)
{
    if (!bytes || size<5) return false;
    size_t pos=5;
    auto readString=[&](std::string& out) {
        if (size-pos<4) return false;
        uint32_t n=uint32_t(bytes[pos]) | uint32_t(bytes[pos+1])<<8 |
            uint32_t(bytes[pos+2])<<16 | uint32_t(bytes[pos+3])<<24;
        pos+=4;
        if (!n || n>size-pos || bytes[pos+n-1]!=0) return false;
        if (std::memchr(bytes+pos,0,n-1)) return false;
        out.assign(reinterpret_cast<char const*>(bytes+pos),n-1); pos+=n; return true;
    };
    if (!readString(name) || size-pos<8) return false;
    pos+=8;
    return readString(message) && size-pos==1; // trailing chat tag
}
}
