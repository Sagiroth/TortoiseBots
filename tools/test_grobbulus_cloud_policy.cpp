#include "../ai/playerbot/GrobbulusCloudPolicy.h"

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

int main()
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
