#include "../ai/playerbot/HealReachPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::HealReachWanted;

int main()
{
    std::cout << "Starting TortoiseBots heal reach policy tests...\n";

    // Out of combat: the full-health tank the value always names does not pull
    // the healer away from its leader (Uldaman, full-health party).
    {
        CHECK(!HealReachWanted(false, 100.0f, 90.0f));
        CHECK(!HealReachWanted(false, 90.0f, 90.0f));
        std::cout << "  [PASS] out of combat: no reach to a member at or above almost-full health\n";
    }

    // Out of combat, a member who really is hurt after a fight still gets the walk.
    {
        CHECK(HealReachWanted(false, 89.0f, 90.0f));
        CHECK(HealReachWanted(false, 20.0f, 90.0f));
        std::cout << "  [PASS] out of combat: reach to a hurt member\n";
    }

    // In combat nothing changes: the healer keeps within reach of the tank
    // even before the first hit.
    {
        CHECK(HealReachWanted(true, 100.0f, 90.0f));
        CHECK(HealReachWanted(true, 40.0f, 90.0f));
        std::cout << "  [PASS] in combat: unchanged\n";
    }

    std::cout << "All heal reach policy tests passed.\n";
    return 0;
}
