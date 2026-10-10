#include "../ai/playerbot/strategy/shaman/ShamanRecallPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::TotemicRecallShouldFire;

int main()
{
    std::cout << "Starting shaman totemic-recall policy tests...\n";

    // Idle with totems down: recall fires.
    CHECK(TotemicRecallShouldFire(true, true, false, false, false) == true);
    std::cout << "  [PASS] idle bots recall standing totems\n";

    // Missing spell or no totems: quiet.
    CHECK(TotemicRecallShouldFire(false, true, false, false, false) == false);
    CHECK(TotemicRecallShouldFire(true, false, false, false, false) == false);
    CHECK(TotemicRecallShouldFire(false, false, false, false, false) == false);
    std::cout << "  [PASS] untrained bots and empty fields stay quiet\n";

    // Mana tide down: never destroy the cooldown totem.
    CHECK(TotemicRecallShouldFire(true, true, true, false, false) == false);
    std::cout << "  [PASS] mana tide is never recalled\n";

    // Any combat anywhere: no recall.
    CHECK(TotemicRecallShouldFire(true, true, false, true, false) == false);
    CHECK(TotemicRecallShouldFire(true, true, false, false, true) == false);
    CHECK(TotemicRecallShouldFire(true, true, false, true, true) == false);
    std::cout << "  [PASS] combat anywhere vetoes the recall\n";

    std::cout << "All shaman totemic-recall policy checks PASSED!\n";
    return 0;
}
