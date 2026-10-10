#include "../ai/playerbot/EquipThresholdPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::EquipUpgradeBetter;

int main()
{
    std::cout << "Starting TortoiseBots equip-upgrade threshold tests...\n";

    // Epsilon gain: no swap (the churn this knob stops).
    CHECK(!EquipUpgradeBetter(102, 100));
    CHECK(!EquipUpgradeBetter(105, 100));
    CHECK(!EquipUpgradeBetter(110, 100));
    std::cout << "  [PASS] epsilon gains do not swap\n";

    // Exact tie: no swap here (caller's tiebreaks own ties).
    CHECK(!EquipUpgradeBetter(100, 100));
    CHECK(!EquipUpgradeBetter(0, 0));
    std::cout << "  [PASS] exact ties do not swap\n";

    // Clear gain above the 1.1x factor: swaps.
    CHECK(EquipUpgradeBetter(111, 100));
    CHECK(EquipUpgradeBetter(150, 100));
    std::cout << "  [PASS] clear gains swap\n";

    // First real stats over nothing always win.
    CHECK(EquipUpgradeBetter(1, 0));
    CHECK(EquipUpgradeBetter(50, 0));
    std::cout << "  [PASS] first stats over zero swap\n";

    // Worse item never swaps, and a 1.0 factor restores the old any-gain
    // behavior for operators who want it.
    CHECK(!EquipUpgradeBetter(90, 100));
    CHECK(EquipUpgradeBetter(101, 100, 1.0f));
    CHECK(!EquipUpgradeBetter(100, 100, 1.0f));
    std::cout << "  [PASS] worse never swaps; 1.0 restores any-gain\n";

    std::cout << "All TortoiseBots equip-upgrade threshold tests passed.\n";
    return 0;
}
