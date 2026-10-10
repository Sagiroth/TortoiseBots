#include "../ai/playerbot/TradeLockboxPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::TradeLockboxUseful;

int main()
{
    std::cout << "Starting TortoiseBots trade-lockbox gate tests...\n";

    // Rogue with a locked box: useful, the pick runs.
    CHECK(TradeLockboxUseful(true, true, true, false));
    std::cout << "  [PASS] rogue with locked box runs\n";

    // Non-rogue: silent on every window update, whatever sits there.
    CHECK(!TradeLockboxUseful(false, true, true, false));
    CHECK(!TradeLockboxUseful(false, true, false, false));
    CHECK(!TradeLockboxUseful(false, false, false, false));
    std::cout << "  [PASS] non-rogue stays silent\n";

    // No box, unlocked box, or unlocked flag: nothing to pick.
    CHECK(!TradeLockboxUseful(true, false, false, false));
    CHECK(!TradeLockboxUseful(true, true, false, false));
    CHECK(!TradeLockboxUseful(true, true, true, true));
    std::cout << "  [PASS] nothing pickable stays quiet\n";

    std::cout << "All TortoiseBots trade-lockbox gate tests passed.\n";
    return 0;
}
