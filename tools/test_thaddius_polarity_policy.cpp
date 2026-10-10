#include "../ai/playerbot/ThaddiusPolarityPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsThaddiusPetActive;
using ai::IsThaddiusPhasePet;
using ai::IsThaddiusPhaseTransition;
using ai::ShouldStopPetDps;
using ai::ThaddiusPolaritySide;

int main()
{
    std::cout << "Starting TortoiseBots Thaddius-polarity policy tests...\n";

    // Fake-dead adds (alive flag off OR not-selectable) are not active.
    CHECK(IsThaddiusPetActive(true, false));
    CHECK(!IsThaddiusPetActive(true, true));
    CHECK(!IsThaddiusPetActive(false, false));
    CHECK(IsThaddiusPhasePet(true, false));
    CHECK(IsThaddiusPhasePet(false, true));
    CHECK(!IsThaddiusPhasePet(false, false));
    // Transition only with pets down and Thaddius still shielded.
    CHECK(IsThaddiusPhaseTransition(false, true));
    CHECK(!IsThaddiusPhaseTransition(true, true));
    CHECK(!IsThaddiusPhaseTransition(false, false));
    std::cout << "  [PASS] phase detection honors the fake-death guard\n";

    // Even-HP gate: stop at <=40% while the other pet leads by 3%+.
    CHECK(ShouldStopPetDps(35.0f, 40.0f));
    CHECK(ShouldStopPetDps(40.0f, 43.0f));
    CHECK(!ShouldStopPetDps(41.0f, 90.0f));
    CHECK(!ShouldStopPetDps(35.0f, 37.0f));
    CHECK(!ShouldStopPetDps(35.0f, 35.0f));
    std::cout << "  [PASS] even-HP DPS gate matches donor rule\n";

    // Polarity sides: negative left, positive right, none center.
    CHECK(ThaddiusPolaritySide(true, false) == 0);
    CHECK(ThaddiusPolaritySide(false, true) == 1);
    CHECK(ThaddiusPolaritySide(false, false) == 2);
    CHECK(ThaddiusPolaritySide(true, true) == 0);
    std::cout << "  [PASS] polarity sides match donor mapping\n";

    std::cout << "All Thaddius-polarity policy tests passed.\n";
    return 0;
}
