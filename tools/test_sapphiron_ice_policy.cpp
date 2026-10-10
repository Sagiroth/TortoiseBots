#include "../ai/playerbot/SapphironIcePolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::SapphironBlizzardExit;
using ai::SapphironHideSpot;

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main()
{
    std::cout << "Starting TortoiseBots Sapphiron-ice policy tests...\n";

    // Hide spot sits 3yd behind the iceblocked member, away from boss.
    float x, y;
    SapphironHideSpot(0.0f, 0.0f, 10.0f, 0.0f, x, y);
    CHECK(Near(x, 13.0f) && Near(y, 0.0f));
    SapphironHideSpot(0.0f, 0.0f, 0.0f, 10.0f, x, y);
    CHECK(Near(x, 0.0f) && Near(y, 13.0f));
    SapphironHideSpot(5.0f, 5.0f, 5.0f, 5.0f, x, y);
    CHECK(Near(x, 8.0f) && Near(y, 5.0f));
    std::cout << "  [PASS] hide spot is behind the iceblock, away from boss\n";

    // Blizzard exit steps 10yd directly away from the NPC.
    SapphironBlizzardExit(0.0f, 0.0f, 6.0f, 8.0f, x, y);
    CHECK(Near(x, 12.0f) && Near(y, 16.0f));
    std::cout << "  [PASS] blizzard exit steps away from the NPC\n";

    std::cout << "All Sapphiron-ice policy tests passed.\n";
    return 0;
}
