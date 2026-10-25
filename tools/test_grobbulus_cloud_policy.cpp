#include "../ai/playerbot/policy/raid/GrobbulusCloudPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::GrobbulusBehindSpot;

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

int UpstreamGrobbulusCloudPolicyTest()
{
    std::cout << "Starting TortoiseBots Grobbulus-cloud policy tests...\n";

    // Behind spot: 18yd opposite the boss facing.
    float x, y;
    GrobbulusBehindSpot(0.0f, 0.0f, 0.0f, x, y);
    CHECK(Near(x, -18.0f) && Near(y, 0.0f));
    GrobbulusBehindSpot(100.0f, 50.0f, 3.14159265f, x, y);
    CHECK(Near(x, 118.0f) && Near(y, 50.0f));
    std::cout << "  [PASS] behind spot is opposite boss facing at 18yd\n";

    std::cout << "All Grobbulus-cloud policy tests passed.\n";
    return 0;
}

#include "../ai/playerbot/policy/raid/GrobbulusCloudPolicy.h"
#include <cassert>
int main()
{
    if (UpstreamGrobbulusCloudPolicyTest()) return 1;
    using namespace ai;
    assert(std::fabs(GrobbulusCloudRadius(60000,60000)-4.9f)<0.01f);
    assert(std::fabs(GrobbulusCloudRadius(60000,30000)-13.9f)<0.01f);
    assert(GrobbulusCloudRadius(60000,0)==22);
    assert(GrobbulusCloudRadius(0,0)==32);
    assert(GrobbulusCloudRadius(60000,-1)==32);
    float last=0;
    for (int remaining=60000;remaining>=0;remaining-=1000)
    {
        float radius=GrobbulusCloudRadius(60000,remaining);
        assert(radius>=last && radius<=22);last=radius;
    }
    // Nearest center is outside its small circle; farther center still contains us.
    std::vector<GrobbulusCloudCircle> clouds={{3,0,1},{10,0,12}};
    assert(GrobbulusCloudContains(0,0,clouds));
    assert(!GrobbulusCloudContains(-4,0,clouds));
    assert(GrobbulusEscapeSample(0,0,-4,0,clouds));
    assert(!GrobbulusEscapeSample(0,0,3,0,clouds));
    std::vector<GrobbulusCloudCircle> overlap={{0,0,10},{5,0,10}};
    assert(GrobbulusCloudContains(2,0,overlap));
    assert(GrobbulusEscapeSample(2,0,2,11,overlap));
    assert(!GrobbulusCloudContains(2,11,overlap));
    assert(!GrobbulusEscapeSample(2,0,0,0,overlap));
}
