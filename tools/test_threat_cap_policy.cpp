// Standalone check for ai/playerbot/ThreatCapPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_threat_cap_policy.cpp -o /tmp/test_threat_cap_policy
//   /tmp/test_threat_cap_policy
#include "../ai/playerbot/ThreatCapPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots threat cap policy tests...\n";

    // ThreatCapIsTheHighestTankByDefault: without a `threat_successor` row the reference is the highest tank, whatever
    // the target's taunt immunity. Tanks {1000, 600}: 1000, not 600.
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true}}, false) == 1000.0f);
    CHECK(ThreatCapReference({{600.0f, false}, {1000.0f, true}}, false) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false}}, false) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true}, {400.0f, true}}, false) == 1000.0f);
    CHECK(ThreatCapReference({}, false) == -1.0f);
    CHECK(!ThreatCapPickReference({{1000.0f, false}, {600.0f, true}}, false).offTank);

    // ThreatCapsAtTheAssignedOffTank: on a target whose boss data asks for a successor (Hakkar) the damage dealers stay
    // below the assigned off-tank so a Cause Insanity threat reset hands the boss to it. Main 1000, off-tank 600: 600.
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true}}, true) == 600.0f);
    // An off-tank already above the main tank stands at its own threat: the cap is the highest off-tank, not always
    // the second number.
    CHECK(ThreatCapReference({{600.0f, false}, {1000.0f, true}}, true) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true}, {400.0f, true}}, true) == 600.0f);
    // Two off-tanks, the higher of them stands.
    CHECK(ThreatCapReference({{1000.0f, false}, {400.0f, true}, {700.0f, true}}, true) == 700.0f);

    // TauntImmuneSuccessorIgnoresHalfFloor: the floor is absolute (1 % of the highest tank, never below 100 threat),
    // not half of the main tank's threat. Tanks at 1000 and 300 cap at the off-tank's 300 — the old 50 % floor sat the
    // cap at 1000 and Cause Insanity then deleted the main tank's threat, moving the boss to the top damage dealer.
    CHECK(kThreatCapOffTankMinShare == 0.01f);
    CHECK(kThreatCapOffTankMinThreat == 100.0f);
    CHECK(ThreatCapReference({{1000.0f, false}, {300.0f, true}}, true) == 300.0f);
    CHECK(ThreatCapPickReference({{1000.0f, false}, {300.0f, true}}, true).offTank);
    // Well above the floor: unchanged by the new floor.
    CHECK(ThreatCapReference({{1000.0f, false}, {600.0f, true}}, true) == 600.0f);

    // ThreatCapIgnoresATinyOffTankThreat: an off-tank below the absolute floor is not the successor — capping the
    // damage dealers there would sit the cap at almost nothing and stop all damage, so the highest tank's threat
    // stands instead. A 50 off-tank on a 1000 main tank is below 100; 101 is above it.
    CHECK(ThreatCapReference({{1000.0f, false}, {50.0f, true}}, true) == 1000.0f);
    CHECK(!ThreatCapPickReference({{1000.0f, false}, {50.0f, true}}, true).offTank);
    CHECK(ThreatCapReference({{1000.0f, false}, {101.0f, true}}, true) == 101.0f);
    // A charmed main tank whose threat is gone leaves the off-tank's 600 as the reference.
    CHECK(ThreatCapReference({{0.0f, false}, {600.0f, true}}, true) == 600.0f);
    // The 1 % share wins over the fixed 100 on a huge main-tank lead: 2 000 000 * 1 % = 20 000.
    CHECK(ThreatCapReference({{2000000.0f, false}, {15000.0f, true}}, true) == 2000000.0f);
    CHECK(ThreatCapReference({{2000000.0f, false}, {25000.0f, true}}, true) == 25000.0f);

    // ThreatCapWithoutAZeroReference: an off-tank with no threat on the target (dead, not yet engaged, or its threat
    // deleted) never becomes the reference: a zero reads as "the tank has no threat" and vetoes every damage action
    // (Hakkar's Cause Insanity zeroing the main tank was the bug). The highest tank stands instead.
    CHECK(ThreatCapReference({{1000.0f, false}, {0.0f, true}}, true) == 1000.0f);
    CHECK(ThreatCapReference({{1000.0f, false}, {0.0f, true}, {0.0f, true}}, true) == 1000.0f);
    // The main tank is charmed and its threat gone; the off-tank still stands as the successor.
    CHECK(ThreatCapReference({{0.0f, false}, {600.0f, true}}, true) == 600.0f);

    // ThreatCapWithoutAnAssignedOffTank: a lone tank (or none of the assigned off-tanks on the target) leaves the
    // highest tank as the reference, never a zero.
    CHECK(ThreatCapReference({{1000.0f, false}}, true) == 1000.0f);
    CHECK(ThreatCapReference({{900.0f, false}, {1000.0f, false}}, true) == 1000.0f);
    CHECK(ThreatCapReference({}, true) == -1.0f);

    std::cout << "threat cap policy: all tests passed\n";
    return 0;
}
