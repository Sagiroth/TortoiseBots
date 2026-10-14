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
using ai::ShouldGrantPoolFlee;
using ai::ShouldIgnoreHardHostile;
using ai::ShouldSeedDrink;
using ai::ShouldDrinkAtManaPct;
using ai::ShouldSelfHealerHoldGround;
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

    // Pool melee hold ground: only the opt-in key plus a warrior/rogue drops
    // the flee strategy; key off (the default) keeps it for every class.
    CHECK(ShouldGrantPoolFlee(false, true));
    CHECK(ShouldGrantPoolFlee(false, false));
    CHECK(ShouldGrantPoolFlee(true, false));
    CHECK(!ShouldGrantPoolFlee(true, true));

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

    // Outnumbered hold ground: only a solo hybrid self-healer with mana at or
    // above LowMana stays; grouped, out of mana or another class keeps flee.
    CHECK(ShouldSelfHealerHoldGround(true, false, true, 100, 15));
    CHECK(ShouldSelfHealerHoldGround(true, false, true, 15, 15));
    CHECK(!ShouldSelfHealerHoldGround(true, false, true, 14, 15));
    CHECK(!ShouldSelfHealerHoldGround(true, false, false, 0, 15));
    CHECK(!ShouldSelfHealerHoldGround(true, true, true, 100, 15));
    CHECK(!ShouldSelfHealerHoldGround(false, false, true, 100, 15));

    // Rest refusal reasons (#916): the first gate that held, in order.
    {
        using ai::RestRefusalInput;
        using ai::RestRefusalReason;
        RestRefusalInput in;
        in.usesMana = true;
        in.manaPct = 40;
        in.healthPct = 100;
        in.alive = false;
        CHECK(RestRefusalReason(in) == "dead");
        in.alive = true;
        in.inCombat = true;
        in.groupFighting = true;
        CHECK(RestRefusalReason(in) == "in combat");
        in.inCombat = false;
        CHECK(RestRefusalReason(in) == "party still fighting");
        in.groupFighting = false;
        in.hasDrink = false;
        CHECK(RestRefusalReason(in) == "no drink");
        in.healthPct = 30;
        in.hasFood = false;
        CHECK(RestRefusalReason(in) == "no food or drink");
        // Food at hand, should-eat false (leader moving), no drink: moving.
        in.hasFood = true;
        in.shouldEat = false;
        CHECK(RestRefusalReason(in) == "leader is moving");
        // Drink at hand but should-drink false, food path also held.
        in.hasDrink = true;
        in.shouldDrink = false;
        CHECK(RestRefusalReason(in) == "leader is moving");
        // Should-values say yes but the action still did not start.
        in.shouldDrink = true;
        in.shouldEat = true;
        CHECK(RestRefusalReason(in) == "other: not useful now");
        // Only the eat side held, the drink side is open: not "moving".
        in.shouldEat = false;
        CHECK(RestRefusalReason(in) == "other: not useful now");
        in.shouldEat = true;
        in.manaPct = 90;
        in.healthPct = 96;
        CHECK(RestRefusalReason(in) == "nothing to rest (mana 90% >= 85%, health 96% >= 50%)");
        in.usesMana = false;
        in.healthPct = 20;
        in.hasFood = false;
        CHECK(RestRefusalReason(in) == "no food");
        in.healthPct = 80;
        CHECK(RestRefusalReason(in) == "nothing to rest (health 80% >= 50%)");

        using ai::FormatRestRefusal;
        CHECK(FormatRestRefusal({}).empty());
        CHECK(FormatRestRefusal({ { "Anna", "" } }) == "No bot rested: other: not useful now: Anna.");
        CHECK(FormatRestRefusal({ { "Anna", "no drink" }, { "Bert", "in combat" }, { "Cara", "no drink" } }) ==
            "No bot rested: no drink: Anna, Cara; in combat: Bert.");

        // A 40-bot raid stays under the cap and says how many were left out.
        std::vector<std::pair<std::string, std::string>> raid;
        for (int i = 0; i < 40; ++i)
            raid.push_back({ "Raidbot" + std::to_string(i), i % 2 ? "no drink" : "party still fighting" });
        std::string const line = FormatRestRefusal(raid);
        CHECK(line.size() <= 240);
        CHECK(line.rfind("No bot rested: party still fighting: Raidbot0, Raidbot2", 0) == 0);
        std::size_t shown = 0;
        for (auto const& entry : raid)
            if (line.find(entry.first + ",") != std::string::npos ||
                line.find(entry.first + ";") != std::string::npos ||
                line.find(entry.first + ".") != std::string::npos)
                ++shown;
        CHECK(shown > 0 && shown < 40);
        CHECK(line.find("; +" + std::to_string(40 - shown) + " more.") != std::string::npos);
        // A cap too small for even one name still reports the count.
        CHECK(FormatRestRefusal(raid, 20) == "No bot rested: +40 more.");
    }

    std::cout << "survive policy: OK\n";
    return 0;
}
