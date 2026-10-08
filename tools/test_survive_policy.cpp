#include "../ai/playerbot/SurvivePolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::DrinkStopManaPct;
using ai::FormatDeathAttackers;
using ai::IsDeathAttackerSnapshotFresh;
using ai::RestStopHealthPct;
using ai::ShouldAnswerAttacker;
using ai::ShouldFleeAtCriticalHealth;
using ai::ShouldIgnoreHardHostile;
using ai::ShouldSeedDrink;
using ai::ShouldDrinkAtManaPct;
using ai::DeathAttackerEntry;
int main()
{
    // Rest stop: cheat bots eat to almost-full, everyone else to low.
    // Start threshold untouched: the trigger band still opens at medium.
    CHECK(RestStopHealthPct(true, 70, 50, 90) == 90);
    CHECK(RestStopHealthPct(false, 70, 50, 90) == 50);
    // Custom tunables flow through, no hardcoded 70/50/90 in callers.
    CHECK(RestStopHealthPct(true, 60, 40, 80) == 80);
    CHECK(RestStopHealthPct(false, 60, 40, 80) == 40);

    // Drink stop is the mana analogue of the eat stop, cheat-gated the same
    // way: cheat bots drink to almost-full, everyone else keeps the old 85.
    CHECK(DrinkStopManaPct(true, 90) == 90);
    CHECK(DrinkStopManaPct(false, 90) == 85);
    CHECK(DrinkStopManaPct(true, 80) == 80);

    // Drink (and water seeding) is mana users only: warrior/rogue get none.
    CHECK(ShouldSeedDrink(true));
    CHECK(!ShouldSeedDrink(false));
    CHECK(ShouldDrinkAtManaPct(true, 0, 85));
    CHECK(ShouldDrinkAtManaPct(true, 84, 85));
    CHECK(!ShouldDrinkAtManaPct(true, 85, 85));
    CHECK(!ShouldDrinkAtManaPct(false, 0, 85));

    // The restored critical-health flee is pool-only: no real player master.
    CHECK(ShouldFleeAtCriticalHealth(false));
    CHECK(!ShouldFleeAtCriticalHealth(true));

    // Attacker snapshot freshness: 30 s window, 0 means no sample yet.
    std::uint32_t const now = 1'000'000;
    CHECK(!IsDeathAttackerSnapshotFresh(now, 0));
    CHECK(IsDeathAttackerSnapshotFresh(now, now));
    CHECK(IsDeathAttackerSnapshotFresh(now, now - 29999));
    CHECK(IsDeathAttackerSnapshotFresh(now, now - 30000));
    CHECK(!IsDeathAttackerSnapshotFresh(now, now - 30001));
    // Wrap-safe: a sample 266 ms before the ~49 d clock wrap is fresh 10 ms
    // after it; a sample 100 s before the wrap is not.
    CHECK(IsDeathAttackerSnapshotFresh(10u, 0xFFFFFF00u));
    CHECK(!IsDeathAttackerSnapshotFresh(100000u, 0xFFFFFF00u));

    // Death formatting keeps the deaths.csv schema: Name(level) entries
    // concatenated, killer excluded by name, count of kept entries.
    {
        std::vector<DeathAttackerEntry> entries = { { "Wolf", 5 }, { "Bear", 7 } };
        auto formatted = FormatDeathAttackers(entries, "Bear");
        CHECK(formatted.first == "Wolf(5)");
        CHECK(formatted.second == 1);
    }
    {
        std::vector<DeathAttackerEntry> entries;
        auto formatted = FormatDeathAttackers(entries, "Wolf");
        CHECK(formatted.first.empty());
        CHECK(formatted.second == 0);
    }
    {
        // Empty killer name keeps everything (environment deaths).
        std::vector<DeathAttackerEntry> entries = { { "Wolf", 5 } };
        auto formatted = FormatDeathAttackers(entries, "");
        CHECK(formatted.first == "Wolf(5)");
        CHECK(formatted.second == 1);
    }
    {
        // Same-kind adds sharing the killer's name stay excluded, as before.
        std::vector<DeathAttackerEntry> entries = { { "Wolf", 5 }, { "Wolf", 6 } };
        auto formatted = FormatDeathAttackers(entries, "Wolf");
        CHECK(formatted.first.empty());
        CHECK(formatted.second == 0);
    }
    {
        // The per-tick copy is bounded: at most 8 entries land in the row.
        std::vector<DeathAttackerEntry> entries;
        for (int i = 0; i < 10; ++i)
            entries.push_back({ "Mob" + std::to_string(i), 5 });
        auto formatted = FormatDeathAttackers(entries, "");
        CHECK(formatted.second == 8);
        CHECK(formatted.first == "Mob0(5)Mob1(5)Mob2(5)Mob3(5)Mob4(5)Mob5(5)Mob6(5)Mob7(5)");
    }

    // Revenge answers whenever the mob holds the bot as victim - the only
    // input; wounds, travel and healer spec are decided by other gates.
    CHECK(ShouldAnswerAttacker(true));
    CHECK(!ShouldAnswerAttacker(false));

    // Hard-mob ignore keeps the old +5 line, but never for a mob already
    // fighting the bot: that one stays valid so revenge can answer.
    CHECK(ShouldIgnoreHardHostile(36, 30, false));
    CHECK(!ShouldIgnoreHardHostile(35, 30, false));
    CHECK(!ShouldIgnoreHardHostile(36, 30, true));
    CHECK(!ShouldIgnoreHardHostile(60, 30, true));
    CHECK(!ShouldIgnoreHardHostile(25, 30, false));

    std::cout << "survive policy: OK\n";
    return 0;
}
