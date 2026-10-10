#include "../ai/playerbot/BgForceResetPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::BgForceResetAnchorKey;
using ai::BgForceResetCooldownSec;
using ai::BgForceResetUseful;

int main()
{
    std::cout << "Starting TortoiseBots bg force-reset gate tests...\n";

    // One-minute cooldown matches the `timer bg` watchdog cadence.
    CHECK(BgForceResetCooldownSec() == 60);
    CHECK(BgForceResetAnchorKey() == std::string("bg force reset at"));
    std::cout << "  [PASS] cooldown and anchor key are fixed\n";

    // Outside a match: never useful, dead or alive.
    CHECK(!BgForceResetUseful(false, false, 0, 1000));
    CHECK(!BgForceResetUseful(false, false, 500, 1000));
    std::cout << "  [PASS] inert outside battlegrounds\n";

    // Mid-fight: never useful, so the full stop cannot feed the enemy a
    // standing target.
    CHECK(!BgForceResetUseful(true, true, 0, 1000));
    CHECK(!BgForceResetUseful(true, true, 500, 1000));
    std::cout << "  [PASS] never mid-fight\n";

    // First reset in a match: useful, dead or alive-out-of-combat.
    CHECK(BgForceResetUseful(true, false, 0, 1000));
    std::cout << "  [PASS] first reset fires\n";

    // Inside the cooldown: quiet, so corpse-run ticks and timer ticks
    // cannot churn the role roll and the path.
    CHECK(!BgForceResetUseful(true, false, 1000, 1000 + BgForceResetCooldownSec() - 1));
    CHECK(!BgForceResetUseful(true, false, 1000, 1000));
    std::cout << "  [PASS] cooldown holds repeat resets\n";

    // Cooldown elapsed: useful again (next death, next watchdog minute).
    CHECK(BgForceResetUseful(true, false, 1000, 1000 + BgForceResetCooldownSec()));
    CHECK(BgForceResetUseful(true, false, 1000, 1000 + 600));
    std::cout << "  [PASS] cooldown release re-arms\n";

    std::cout << "All TortoiseBots bg force-reset gate tests passed.\n";
    return 0;
}
