#include "../ai/playerbot/policy/raid/OnyxiaBreathPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::BreathAxisFromFacing;
using ai::BreathAxisIndex;
using ai::IsOnyxiaBreathSpell;
using ai::kBreathSafeZoneRadius;
using ai::kOnyxiaEntry;
using ai::ShouldMoveToBreathSafeZone;

int UpstreamOnyxiaBreathPolicyTest()
{
    std::cout << "Starting TortoiseBots onyxia-breath policy tests...\n";

    CHECK(kOnyxiaEntry == 10184);
    CHECK(kBreathSafeZoneRadius == 5.0f);
    std::cout << "  [PASS] entry and radius pinned\n";

    // All 8 breath ids recognized; neighbors rejected.
    CHECK(IsOnyxiaBreathSpell(17086));
    CHECK(IsOnyxiaBreathSpell(18351));
    CHECK(IsOnyxiaBreathSpell(18576));
    CHECK(IsOnyxiaBreathSpell(18609));
    CHECK(IsOnyxiaBreathSpell(18564));
    CHECK(IsOnyxiaBreathSpell(18584));
    CHECK(IsOnyxiaBreathSpell(18596));
    CHECK(IsOnyxiaBreathSpell(18617));
    CHECK(!IsOnyxiaBreathSpell(18392));
    CHECK(!IsOnyxiaBreathSpell(0));
    std::cout << "  [PASS] all 8 breath ids recognized\n";

    // Axis pairing: opposite directions share safe zones.
    CHECK(BreathAxisIndex(17086) == 0);
    CHECK(BreathAxisIndex(18351) == 0);
    CHECK(BreathAxisIndex(18576) == 1);
    CHECK(BreathAxisIndex(18609) == 1);
    CHECK(BreathAxisIndex(18564) == 2);
    CHECK(BreathAxisIndex(18584) == 2);
    CHECK(BreathAxisIndex(18596) == 3);
    CHECK(BreathAxisIndex(18617) == 3);
    CHECK(BreathAxisIndex(18392) == -1);
    std::cout << "  [PASS] axis pairing correct\n";

    // Facing maps to lane axes. Angles are atan2(dy,dx): 0=N(+X), PI/2=W(+Y).
    // Diagonals: eighth 3 (SW) + 7 (NE) -> axis 3; eighth 5 (SE) + 1 (NW) -> axis 2.
    CHECK(BreathAxisFromFacing(0.0f) == 0);
    CHECK(BreathAxisFromFacing(3.1415926536f) == 0);
    CHECK(BreathAxisFromFacing(1.5707963268f) == 1);
    CHECK(BreathAxisFromFacing(4.7123889804f) == 1);
    CHECK(BreathAxisFromFacing(2.3561944902f) == 3);
    CHECK(BreathAxisFromFacing(5.4977871438f) == 3);
    CHECK(BreathAxisFromFacing(3.9269908170f) == 2);
    CHECK(BreathAxisFromFacing(0.7853981634f) == 2);
    CHECK(BreathAxisFromFacing(6.2831853072f) == 0);
    std::cout << "  [PASS] facing-to-axis mapping\n";

    // Move only while breath casts and not already safe.
    CHECK(ShouldMoveToBreathSafeZone(true, false));
    CHECK(!ShouldMoveToBreathSafeZone(true, true));
    CHECK(!ShouldMoveToBreathSafeZone(false, false));
    std::cout << "  [PASS] move/hold rule\n";

    std::cout << "All onyxia-breath policy tests passed.\n";
    return 0;
}

#include "../ai/playerbot/policy/raid/OnyxiaBreathPolicy.h"
#include <cassert>
int main()
{
    if (UpstreamOnyxiaBreathPolicyTest()) return 1;
    using namespace ai;
    for (auto const& launch:kOnyxiaBreathLaunches)
        assert(OnyxiaBreathDirection(launch.x,launch.y)==launch.spell);
    assert(!OnyxiaBreathDirection(-25,-215));
    assert(OnyxiaBreathFresh(5,0xfffffff0u));
    assert(!OnyxiaBreathFresh(15000,0));
    assert(!OnyxiaBreathFloor(-30.127f,-254.463f,-89.44f));
    assert(!OnyxiaBreathFloor(-30.817f,-177.106f,-89.258f));
    assert(!OnyxiaBreathFloor(-25,-215,-65));
    assert(!OnyxiaBreathFloor(-100,-215,-89));
    assert(OnyxiaBreathFloor(-25,-215,-89));
    assert(OnyxiaBreathClearance(0,0,{})<0);
    // Pinned spell_target_position fixtures: test the entire damage chain for
    // all eight directions, including the bent NW/SE chain and long N/S tail.
    {
        std::vector<OnyxiaBreathCircle> circles = {
            {12.120900f,-243.439000f,30.0f},
            {7.197780f,-239.643000f,30.0f},
            {2.736300f,-236.202000f,30.0f},
            {-2.010260f,-232.542000f,30.0f},
            {-6.475300f,-229.099000f,30.0f},
            {-12.223200f,-224.666000f,30.0f},
            {-20.098100f,-218.681000f,30.0f},
            {-30.907600f,-211.058000f,30.0f},
            {-40.500200f,-203.001000f,30.0f},
            {-46.135500f,-198.549000f,30.0f},
            {-52.006300f,-193.797000f,30.0f},
            {-58.250900f,-189.020000f,30.0f},
        };
        unsigned safe=0;
        for (float x=-64;x<=22;x+=2)
            for (float y=-250;y<=-180;y+=2)
                if (OnyxiaBreathFloor(x,y,-89) && OnyxiaBreathClearance(x,y,circles)>=3) ++safe;
        assert(safe>0);
        for (auto const& circle:circles) assert(OnyxiaBreathClearance(circle.x,circle.y,circles)<0);
    }
    {
        std::vector<OnyxiaBreathCircle> circles = {
            {-37.743900f,-243.668000f,30.0f},
            {-35.805300f,-232.029000f,30.0f},
            {-34.045700f,-224.715000f,30.0f},
            {-32.081600f,-214.917000f,30.0f},
            {-36.611700f,-202.685000f,30.0f},
            {-37.067300f,-195.759000f,30.0f},
            {-37.728500f,-188.617000f,30.0f},
        };
        unsigned safe=0;
        for (float x=-64;x<=22;x+=2)
            for (float y=-250;y<=-180;y+=2)
                if (OnyxiaBreathFloor(x,y,-89) && OnyxiaBreathClearance(x,y,circles)>=3) ++safe;
        assert(safe>0);
        for (auto const& circle:circles) assert(OnyxiaBreathClearance(circle.x,circle.y,circles)<0);
    }
    {
        std::vector<OnyxiaBreathCircle> circles = {
            {-56.559700f,-241.224000f,30.0f},
            {-51.067500f,-228.910000f,30.0f},
            {-41.762100f,-221.897000f,30.0f},
            {-35.439900f,-217.260000f,30.0f},
            {-29.495900f,-213.014000f,30.0f},
            {-23.650300f,-221.969000f,30.0f},
            {-15.602100f,-216.894000f,30.0f},
            {-14.321200f,-199.462000f,30.0f},
            {-8.449300f,-193.958000f,30.0f},
            {-2.529650f,-188.690000f,30.0f},
            {3.860220f,-183.227000f,30.0f},
            {6.016710f,-181.306000f,30.0f},
        };
        unsigned safe=0;
        for (float x=-64;x<=22;x+=2)
            for (float y=-250;y<=-180;y+=2)
                if (OnyxiaBreathFloor(x,y,-89) && OnyxiaBreathClearance(x,y,circles)>=3) ++safe;
        assert(safe>0);
        for (auto const& circle:circles) assert(OnyxiaBreathClearance(circle.x,circle.y,circles)<0);
    }
    {
        std::vector<OnyxiaBreathCircle> circles = {
            {-68.834200f,-215.036000f,30.0f},
            {-61.834300f,-215.052000f,30.0f},
            {-53.343300f,-215.071000f,30.0f},
            {-42.619300f,-215.095000f,30.0f},
            {-35.899300f,-215.110000f,30.0f},
            {-28.248300f,-215.127000f,30.0f},
            {-20.324400f,-215.145000f,30.0f},
            {-11.189400f,-215.166000f,30.0f},
            {-2.047410f,-215.186000f,30.0f},
            {7.479570f,-215.208000f,30.0f},
            {20.730500f,-215.238000f,30.0f},
        };
        unsigned safe=0;
        for (float x=-64;x<=22;x+=2)
            for (float y=-250;y<=-180;y+=2)
                if (OnyxiaBreathFloor(x,y,-89) && OnyxiaBreathClearance(x,y,circles)>=3) ++safe;
        assert(safe>0);
        for (auto const& circle:circles) assert(OnyxiaBreathClearance(circle.x,circle.y,circles)<0);
    }
    {
        std::vector<OnyxiaBreathCircle> circles = {
            {-58.250900f,-189.020000f,30.0f},
            {-52.006300f,-193.797000f,30.0f},
            {-46.135500f,-198.549000f,30.0f},
            {-40.500200f,-203.001000f,30.0f},
            {-30.907600f,-211.058000f,30.0f},
            {-20.098100f,-218.681000f,30.0f},
            {-12.223200f,-224.666000f,30.0f},
            {-6.475300f,-229.099000f,30.0f},
            {-2.010260f,-232.542000f,30.0f},
            {2.736300f,-236.202000f,30.0f},
            {7.197780f,-239.643000f,30.0f},
            {12.120900f,-243.439000f,30.0f},
        };
        unsigned safe=0;
        for (float x=-64;x<=22;x+=2)
            for (float y=-250;y<=-180;y+=2)
                if (OnyxiaBreathFloor(x,y,-89) && OnyxiaBreathClearance(x,y,circles)>=3) ++safe;
        assert(safe>0);
        for (auto const& circle:circles) assert(OnyxiaBreathClearance(circle.x,circle.y,circles)<0);
    }
    {
        std::vector<OnyxiaBreathCircle> circles = {
            {-37.728500f,-188.617000f,30.0f},
            {-37.067300f,-195.759000f,30.0f},
            {-36.611700f,-202.685000f,30.0f},
            {-32.081600f,-214.917000f,30.0f},
            {-34.045700f,-224.715000f,30.0f},
            {-35.805300f,-232.029000f,30.0f},
            {-37.743900f,-243.668000f,30.0f},
        };
        unsigned safe=0;
        for (float x=-64;x<=22;x+=2)
            for (float y=-250;y<=-180;y+=2)
                if (OnyxiaBreathFloor(x,y,-89) && OnyxiaBreathClearance(x,y,circles)>=3) ++safe;
        assert(safe>0);
        for (auto const& circle:circles) assert(OnyxiaBreathClearance(circle.x,circle.y,circles)<0);
    }
    {
        std::vector<OnyxiaBreathCircle> circles = {
            {6.016710f,-181.306000f,30.0f},
            {3.860220f,-183.227000f,30.0f},
            {-2.529650f,-188.690000f,30.0f},
            {-8.449300f,-193.958000f,30.0f},
            {-14.321200f,-199.462000f,30.0f},
            {-15.602100f,-216.894000f,30.0f},
            {-23.650300f,-221.969000f,30.0f},
            {-29.495900f,-213.014000f,30.0f},
            {-35.439900f,-217.260000f,30.0f},
            {-41.762100f,-221.897000f,30.0f},
            {-51.067500f,-228.910000f,30.0f},
            {-56.559700f,-241.224000f,30.0f},
        };
        unsigned safe=0;
        for (float x=-64;x<=22;x+=2)
            for (float y=-250;y<=-180;y+=2)
                if (OnyxiaBreathFloor(x,y,-89) && OnyxiaBreathClearance(x,y,circles)>=3) ++safe;
        assert(safe>0);
        for (auto const& circle:circles) assert(OnyxiaBreathClearance(circle.x,circle.y,circles)<0);
    }
    {
        std::vector<OnyxiaBreathCircle> circles = {
            {20.730500f,-215.238000f,30.0f},
            {7.479570f,-215.208000f,30.0f},
            {-2.047410f,-215.186000f,30.0f},
            {-11.189400f,-215.166000f,30.0f},
            {-20.324400f,-215.145000f,30.0f},
            {-28.248300f,-215.127000f,30.0f},
            {-35.899300f,-215.110000f,30.0f},
            {-42.619300f,-215.095000f,30.0f},
            {-53.343300f,-215.071000f,30.0f},
            {-61.834300f,-215.052000f,30.0f},
            {-68.834200f,-215.036000f,30.0f},
            {-75.736000f,-214.985000f,30.0f},
            {-84.087600f,-214.858000f,30.0f},
            {-90.424400f,-214.602000f,30.0f},
            {-96.572400f,-214.354000f,30.0f},
            {-102.070000f,-214.132000f,30.0f},
            {-107.386000f,-213.917000f,30.0f},
            {-114.281000f,-213.866000f,30.0f},
            {-123.329000f,-213.608000f,30.0f},
            {-130.788000f,-213.424000f,30.0f},
        };
        unsigned safe=0;
        for (float x=-64;x<=22;x+=2)
            for (float y=-250;y<=-180;y+=2)
                if (OnyxiaBreathFloor(x,y,-89) && OnyxiaBreathClearance(x,y,circles)>=3) ++safe;
        assert(safe>0);
        for (auto const& circle:circles) assert(OnyxiaBreathClearance(circle.x,circle.y,circles)<0);
    }
    std::vector<uint8_t> packet={0,0,0,0,0};
    auto string=[&](char const* text) {
        uint32_t n=std::strlen(text)+1;
        packet.push_back(n);packet.push_back(n>>8);packet.push_back(n>>16);packet.push_back(n>>24);
        packet.insert(packet.end(),text,text+n);
    };
    string("Onyxia"); packet.insert(packet.end(),8,0); string("warning");packet.push_back(0);
    std::string name,message;
    assert(ReadOnyxiaBossEmote(packet.data(),packet.size(),name,message));
    assert(name=="Onyxia" && message=="warning");
    for (size_t n=0;n<packet.size();++n) assert(!ReadOnyxiaBossEmote(packet.data(),n,name,message));
    packet[5]=255;assert(!ReadOnyxiaBossEmote(packet.data(),packet.size(),name,message));
}
