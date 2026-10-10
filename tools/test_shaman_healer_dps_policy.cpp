#include "../ai/playerbot/strategy/shaman/ShamanHealerDpsPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsRestoHealerDpsAction;
using ai::RestoHealerDpsPriority;

int main()
{
    std::cout << "Starting shaman healer-dps policy tests...\n";

    // The ladder: flame shock (DoT) above lightning bolt (filler), chain
    // lightning highest (pack-only node, fires rarely).
    CHECK(RestoHealerDpsPriority("flame shock") > RestoHealerDpsPriority("lightning bolt"));
    CHECK(RestoHealerDpsPriority("chain lightning") > RestoHealerDpsPriority("flame shock"));
    std::cout << "  [PASS] flame shock outranks filler, pack AoE tops the ladder\n";

    // All three ladder actions stay near ACTION_DEFAULT (lowest relevance):
    // every heal (LIGHT/MEDIUM/CRITICAL at 60/70/80) must outbid them.
    CHECK(RestoHealerDpsPriority("flame shock") < 1.0f);
    CHECK(RestoHealerDpsPriority("chain lightning") < 1.0f);
    CHECK(RestoHealerDpsPriority("lightning bolt") < 1.0f);
    std::cout << "  [PASS] all dps actions stay far below heal priorities\n";

    // Membership: exactly the three wired actions.
    CHECK(IsRestoHealerDpsAction("flame shock"));
    CHECK(IsRestoHealerDpsAction("lightning bolt"));
    CHECK(IsRestoHealerDpsAction("chain lightning"));
    CHECK(!IsRestoHealerDpsAction("earth shock"));
    CHECK(!IsRestoHealerDpsAction("healing wave"));
    CHECK(RestoHealerDpsPriority("earth shock") < -999.0f);
    std::cout << "  [PASS] ladder holds exactly the three wired actions\n";

    std::cout << "All shaman healer-dps policy checks PASSED!\n";
    return 0;
}
