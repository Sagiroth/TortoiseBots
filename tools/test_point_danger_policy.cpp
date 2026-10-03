#include "../ai/playerbot/PointDangerPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::PointDangerApplies;
using ai::PointDangerous;

int main()
{
    std::cout << "Starting TortoiseBots point-danger gate tests...\n";

    // -------------------------------------------------------------
    // (1) Scope: pool bots below 10 only. Owned/hired bots follow
    // their player; level 10+ keeps the old area/route gates.
    // -------------------------------------------------------------
    {
        CHECK(PointDangerApplies(1, true));
        CHECK(PointDangerApplies(4, true));
        CHECK(PointDangerApplies(9, true));
        CHECK(!PointDangerApplies(10, true));
        CHECK(!PointDangerApplies(60, true));
        CHECK(!PointDangerApplies(2, false));
        CHECK(!PointDangerApplies(4, false));
        std::cout << "  [PASS] gate binds masterless bots below 10 only\n";
    }

    // -------------------------------------------------------------
    // Live case: level-4 bot on the item-750 trip (Timber Wolf
    // entry 69, level_max 2, in cap) at POINT(-73.97 -9254.43):
    // Forest Spider 6 / Mangy Wolf 6 share the 40 yd field and bar
    // the point, while a Defias Cutpurse 5 (one above, inside the
    // +1 order cap the bot may fight alone) and a Young Wolf 1 do
    // not. The bot's own level always fits.
    {
        CHECK(PointDangerous(6, 4));
        CHECK(!PointDangerous(5, 4));
        CHECK(!PointDangerous(4, 4));
        CHECK(!PointDangerous(2, 4));
        CHECK(!PointDangerous(1, 4));
        std::cout << "  [PASS] level-4 point bars 6+, keeps 5 and below\n";
    }

    // -------------------------------------------------------------
    // (3) The ceiling travels with the bot, like the order cap:
    // sub-10 pool cap is +1, so +1 fits and +2 bars.
    // -------------------------------------------------------------
    {
        CHECK(!PointDangerous(3, 1));   // valley level-3 hostiles do not bar a level-1 bot
        CHECK(PointDangerous(4, 1));
        CHECK(!PointDangerous(2, 1));
        CHECK(PointDangerous(5, 3));
        CHECK(!PointDangerous(4, 3));
        CHECK(PointDangerous(11, 9));
        CHECK(!PointDangerous(10, 9));
        std::cout << "  [PASS] ceiling travels with the bot (+1 below 10)\n";
    }

    std::cout << "All point-danger gate checks PASSED!\n";
    return 0;
}
