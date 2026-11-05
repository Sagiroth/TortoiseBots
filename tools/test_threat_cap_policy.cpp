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

    // TauntImmuneBossCapsAtOffTank: a normal target caps damage at the highest tank's threat. On a taunt-immune
    // target (Hakkar: creature_template 14834 immunity_flags 6 keeps CREATURE_IMMUNITY_TAUNT) no tank can take the
    // boss back, so the reference is the second-highest tank's threat and that tank stays second: a Cause Insanity
    // then hands the boss to the off-tank instead of a damage dealer. Tanks {1000, 600}: 600, not 1000.
    CHECK(ThreatCapReference({1000.0f, 600.0f}, false) == 1000.0f);
    CHECK(ThreatCapReference({1000.0f, 600.0f}, true) == 600.0f);
    CHECK(ThreatCapReference({600.0f, 1000.0f}, true) == 600.0f);
    CHECK(ThreatCapReference({1000.0f, 600.0f, 400.0f}, true) == 600.0f);
    CHECK(ThreatCapReference({1000.0f, 1000.0f}, true) == 1000.0f);
    // With fewer than two tanks there is no off-tank to cap at: the highest stands.
    CHECK(ThreatCapReference({1000.0f}, true) == 1000.0f);
    CHECK(ThreatCapReference({}, true) == -1.0f);
    CHECK(ThreatCapReference({}, false) == -1.0f);

    std::cout << "threat cap policy: all tests passed\n";
    return 0;
}
