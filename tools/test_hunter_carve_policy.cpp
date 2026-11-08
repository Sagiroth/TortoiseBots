#include "../ai/playerbot/HunterAoePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::HunterCarveAllowed;

int main()
{
    {
        CHECK(!HunterCarveAllowed(true, 15.0f));
        std::cout << "  [PASS] walk54_hakkar_tank_held_15yd_no_carve\n";
    }
    {
        CHECK(!HunterCarveAllowed(true, 7.0f));
        std::cout << "  [PASS] flag_on_dead_zone_edge_7yd_no_carve\n";
    }
    {
        CHECK(HunterCarveAllowed(true, 0.0f));
        CHECK(HunterCarveAllowed(true, 4.9f));
        CHECK(HunterCarveAllowed(true, 5.0f));
        std::cout << "  [PASS] flag_on_in_reach\n";
    }
    {
        CHECK(!HunterCarveAllowed(true, 5.1f));
        std::cout << "  [PASS] flag_on_just_out\n";
    }
    {
        CHECK(HunterCarveAllowed(false, 15.0f));
        CHECK(HunterCarveAllowed(false, 40.0f));
        std::cout << "  [PASS] flag_off_keeps_old_walk\n";
    }
    {
        CHECK(ai::kHunterCarveReachYd == 5.0f);
        std::cout << "  [PASS] reach constant is ATTACK_DISTANCE\n";
    }
    std::cout << "All TortoiseBots hunter carve tests passed!\n";
    return 0;
}
