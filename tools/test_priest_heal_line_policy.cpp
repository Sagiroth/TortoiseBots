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

using ai::PriestAnyHealRankAffordable;
using ai::PriestSuppressLesserHeal;

int main()
{
    std::cout << "Starting TortoiseBots priest-heal-line policy tests...\n";

    CHECK(!PriestSuppressLesserHeal(false, false));
    CHECK(!PriestSuppressLesserHeal(true, false));
    CHECK(!PriestSuppressLesserHeal(false, true));
    CHECK(PriestSuppressLesserHeal(true, true));

    // PriestAnyHealRankAffordable: mana against the cost of each KNOWN Heal rank 2+ (walk 55, Durgrim: Lesser Heal cast
    // 13 and 6 times with Heal known, because CanCastSpell("heal") also fails while Heal is busy).
    {
        // Costs as the core prices them for a level-34 priest: Heal rank 2 (2055) 155, rank 3 (6063) 205, rank 4 (6064) 255.
        // These are only inputs; the rule is "mana >= the cheapest known cost".
        const std::uint32_t knowsRank4Only[] = {255};
        const std::uint32_t knowsAll[] = {155, 205, 255};

        // Failing case: the priest knows 2055, 6063 and 6064, its mana (160) covers 2055 (155) but not 6064 (255) ->
        // suppressed. Before the change "heal" resolved to 6064 alone, CanCastSpell failed on its price and Lesser Heal
        // got through.
        CHECK(PriestAnyHealRankAffordable(knowsAll, 3, 160));
        CHECK(PriestSuppressLesserHeal(true, PriestAnyHealRankAffordable(knowsAll, 3, 160)));
        // A priest whose only known rank 2+ is 6064 must pay 6064 itself.
        CHECK(!PriestAnyHealRankAffordable(knowsRank4Only, 1, 160));
        CHECK(PriestAnyHealRankAffordable(knowsRank4Only, 1, 255));
        // Mana equal to the cost pays it (the core refuses only when power < cost).
        CHECK(PriestAnyHealRankAffordable(knowsAll, 3, 155));
        CHECK(!PriestAnyHealRankAffordable(knowsAll, 3, 154));
        // Cannot pay even the cheapest known rank: Lesser Heal stays as the low-mana fallback.
        CHECK(!PriestSuppressLesserHeal(true, PriestAnyHealRankAffordable(knowsAll, 3, 100)));
        CHECK(!PriestSuppressLesserHeal(true, PriestAnyHealRankAffordable(knowsAll, 3, 0)));
        // No Heal rank 2+ learned: nothing to afford, not suppressed.
        CHECK(!PriestAnyHealRankAffordable(knowsAll, 0, 5000));
        CHECK(!PriestSuppressLesserHeal(false, PriestAnyHealRankAffordable(knowsAll, 0, 5000)));
        // The cheapest known rank decides, wherever it sits in the list.
        const std::uint32_t unordered[] = {255, 155, 205};
        CHECK(PriestAnyHealRankAffordable(unordered, 3, 155));
    }

    std::cout << "All priest-heal-line policy tests passed.\n";
    return 0;
}
