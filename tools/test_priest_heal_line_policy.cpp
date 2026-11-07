// Priest Lesser Heal suppression: only automatic selection drops Lesser
// Heal, and only while the bot knows Heal rank 2+ and could pay for the
// Heal cast now. Decision lives in
// ai/playerbot/policy/class/PriestHealLinePolicy.h.
//
// Build: g++ -std=c++17 -Wall -Wextra tools/test_priest_heal_line_policy.cpp -o /tmp/test_priest_heal_line

#include "../ai/playerbot/policy/class/PriestHealLinePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::PriestSuppressLesserHeal;

int main()
{
    std::cout << "Starting TortoiseBots priest-heal-line policy tests...\n";

    CHECK(!PriestSuppressLesserHeal(false, false));
    CHECK(!PriestSuppressLesserHeal(true, false));
    CHECK(!PriestSuppressLesserHeal(false, true));
    CHECK(PriestSuppressLesserHeal(true, true));

    std::cout << "All priest-heal-line policy tests passed.\n";
    return 0;
}
