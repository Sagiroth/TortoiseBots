// Priest Lesser Heal suppression: only automatic selection drops Lesser
// Heal, and only while the bot knows Heal rank 2+ and could pay for the
// Heal cast now. Decision lives in
// ai/playerbot/policy/class/PriestHealLinePolicy.h.
//
// Build: g++ -std=c++17 -Wall -Wextra tools/test_priest_heal_line_policy.cpp -o /tmp/test_priest_heal_line

#include "../ai/playerbot/policy/class/PriestHealLinePolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::PriestProbedHealAffordable;
using ai::PriestSlowHealProbeSpell;
using ai::PriestSuppressLesserHeal;

int main()
{
    std::cout << "Starting TortoiseBots priest-heal-line policy tests...\n";

    CHECK(!PriestSuppressLesserHeal(false, false));
    CHECK(!PriestSuppressLesserHeal(true, false));
    CHECK(!PriestSuppressLesserHeal(false, true));
    CHECK(PriestSuppressLesserHeal(true, true));

    // PriestProbedHealAffordable: mana against the cost of the rank the "heal" action PROBES (the top known rank; "heal
    // on party" clears the rank override and the downrank happens only in Execute, after the probe). Walk 55, Durgrim:
    // Lesser Heal cast 13 and 6 times with Heal known, because CanCastSpell("heal") also fails while Heal is busy; the
    // fix prices the probed rank by mana alone. Costs for a level-34 priest: Heal rank 2 (2055) 155, rank 3 (6063)
    // 205, rank 4 (6064) 255. They are only inputs; the rule is "mana >= the probed rank's cost".
    {
        const std::uint32_t rank2 = 155;
        const std::uint32_t rank4 = 255;

        // top_rank_unaffordable_keeps_lesser_heal (the failing case): ranks 2-4 known, the probed rank is 6064 (255),
        // mana 160 covers rank 2 but not the probed rank -> NOT suppressed. Heal is refused on its price in that band,
        // so Lesser Heal must stay as the fallback; the first version of this fix priced rank 2 and left the priest
        // with no heal at all.
        CHECK(!PriestSuppressLesserHeal(true, PriestProbedHealAffordable(rank4, 160)));
        CHECK(160 >= rank2);
        CHECK(!PriestSuppressLesserHeal(true, PriestProbedHealAffordable(rank4, 254)));
        // Mana covers the probed top rank -> suppressed (and equal to the cost pays it).
        CHECK(PriestSuppressLesserHeal(true, PriestProbedHealAffordable(rank4, 255)));
        CHECK(PriestSuppressLesserHeal(true, PriestProbedHealAffordable(rank4, 1000)));
        // A priest whose top known rank is 2 (level 22-27) pays rank 2.
        CHECK(!PriestSuppressLesserHeal(true, PriestProbedHealAffordable(rank2, 154)));
        CHECK(PriestSuppressLesserHeal(true, PriestProbedHealAffordable(rank2, 155)));
        // Cannot pay at all: Lesser Heal stays.
        CHECK(!PriestSuppressLesserHeal(true, PriestProbedHealAffordable(rank4, 0)));
        // No Heal rank 2+ learned: not suppressed whatever the mana.
        CHECK(!PriestSuppressLesserHeal(false, PriestProbedHealAffordable(rank2, 5000)));
    }

    // The wand probe asks for the same spell the multiplier lets through: "heal" while Lesser Heal is suppressed
    // (mana covers the probed rank), "lesser heal" in the band below it, where the bot will cast Lesser Heal.
    {
        const std::uint32_t rank4 = 255;
        CHECK(std::string(PriestSlowHealProbeSpell(PriestSuppressLesserHeal(true, PriestProbedHealAffordable(rank4, 160)))) ==
            "lesser heal");
        CHECK(std::string(PriestSlowHealProbeSpell(PriestSuppressLesserHeal(true, PriestProbedHealAffordable(rank4, 255)))) ==
            "heal");
        CHECK(std::string(PriestSlowHealProbeSpell(PriestSuppressLesserHeal(false, true))) == "lesser heal");
    }

    std::cout << "All priest-heal-line policy tests passed.\n";
    return 0;
}
