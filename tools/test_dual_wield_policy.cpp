#include "../ai/playerbot/DualWieldPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::DualWieldCascade;

int main()
{
    std::cout << "Starting TortoiseBots dual-wield cascade tests...\n";

    // Empty off hand + fitting old hand: demote (stats never sit in bags).
    CHECK(DualWieldCascade(true, true, true, 100, 0));
    std::cout << "  [PASS] empty off hand demotes\n";

    // Lighter off hand: demote; tied or heavier stays.
    CHECK(DualWieldCascade(true, true, false, 200, 100));
    CHECK(!DualWieldCascade(true, true, false, 100, 100));
    CHECK(!DualWieldCascade(true, true, false, 100, 200));
    std::cout << "  [PASS] only a lighter off hand demotes\n";

    // A 2H old hand never demotes (it cannot go in the off hand).
    CHECK(!DualWieldCascade(false, true, true, 500, 0));
    CHECK(!DualWieldCascade(false, true, false, 500, 100));
    std::cout << "  [PASS] two-hander never demotes\n";

    // Spec-illegal off hand (shield spec, no dual wield): never demotes.
    CHECK(!DualWieldCascade(true, false, true, 500, 0));
    CHECK(!DualWieldCascade(true, false, false, 500, 100));
    std::cout << "  [PASS] spec-illegal off hand never demotes\n";

    std::cout << "All TortoiseBots dual-wield cascade tests passed.\n";
    return 0;
}
