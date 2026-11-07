// Portal policy unit test: city matching, refusal order, service formatting.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_mage_portal_policy.cpp -o /tmp/test_mage_portal_policy
//   /tmp/test_mage_portal_policy

#include "../ai/playerbot/MagePortalPolicy.h"

#include <cstdio>

#define CHECK(x) do { \
    if (!(x)) { \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
        return 1; \
    } \
} while (0)

int main()
{
    // Table: 13 castable portals, 1851 excluded.
    size_t count = 0;
    ai::MagePortalSpell const* table = ai::MagePortalTable(&count);
    CHECK(count == 13);
    for (size_t i = 0; i < count; ++i)
        CHECK(table[i].spellId != 1851);

    // Exact match, any case.
    CHECK(ai::MatchPortalCity("Ironforge") == 11416);
    CHECK(ai::MatchPortalCity("ironforge") == 11416);
    CHECK(ai::MatchPortalCity("STORMWIND") == 10059);
    CHECK(ai::MatchPortalCity("Alah'Thalas") == 49004);

    // Unique prefix matches.
    CHECK(ai::MatchPortalCity("iron") == 11416);
    CHECK(ai::MatchPortalCity("thera") == 49365);
    CHECK(ai::MatchPortalCity("kara") == 28148);

    // Unknown or ambiguous queries refuse.
    CHECK(ai::MatchPortalCity("Moon") == 0);
    CHECK(ai::MatchPortalCity("") == 0);
    CHECK(ai::MatchPortalCity("   ") == 0);
    CHECK(ai::MatchPortalCity("s") == 0);

    // Refusal order: dead > combat > casting > not-mage > unknown > not-learned.
    // Moving never refuses: the caller stops the bot first.
    ai::PortalCastFacts ok;
    CHECK(ai::CheckPortalCast(ok, 11416, true) == ai::PortalRefusal::None);
    ai::PortalCastFacts dead = ok;
    dead.alive = false;
    dead.inCombat = true;
    CHECK(ai::CheckPortalCast(dead, 11416, true) == ai::PortalRefusal::Dead);
    ai::PortalCastFacts combat = ok;
    combat.inCombat = true;
    combat.casting = true;
    CHECK(ai::CheckPortalCast(combat, 11416, true) == ai::PortalRefusal::InCombat);
    ai::PortalCastFacts casting = ok;
    casting.casting = true;
    CHECK(ai::CheckPortalCast(casting, 11416, true) == ai::PortalRefusal::Casting);
    ai::PortalCastFacts nonMage = ok;
    nonMage.isMage = false;
    CHECK(ai::CheckPortalCast(nonMage, 11416, true) == ai::PortalRefusal::NotMage);
    CHECK(ai::CheckPortalCast(ok, 0, false) == ai::PortalRefusal::UnknownCity);
    CHECK(ai::CheckPortalCast(ok, 11416, false) == ai::PortalRefusal::NotLearned);
    ai::PortalCastFacts ungrouped = ok;
    ungrouped.requesterGrouped = false;
    CHECK(ai::CheckPortalCast(ungrouped, 11416, true) == ai::PortalRefusal::NotGrouped);
    CHECK(ai::CheckPortalCast(ungrouped, 11416, false) == ai::PortalRefusal::NotLearned);
    CHECK(std::string(ai::PortalRefusalCode(ai::PortalRefusal::NotGrouped)) == "group");

    // Shared decision: clean cast grants rune and stops when moving; a
    // running cast refuses with code busy; refusals grant nothing.
    ai::PortalCastDecision go = ai::DecidePortalCast(ok, 11416, true, false, true);
    CHECK(go.refusal == ai::PortalRefusal::None && go.needsRune && go.needsStop);
    ai::PortalCastDecision settled = ai::DecidePortalCast(ok, 11416, true, true, false);
    CHECK(settled.refusal == ai::PortalRefusal::None && !settled.needsRune && !settled.needsStop);
    ai::PortalCastDecision refused = ai::DecidePortalCast(casting, 11416, true, false, true);
    CHECK(refused.refusal == ai::PortalRefusal::Casting && !refused.needsRune && !refused.needsStop);

    // Refusal codes match the .bot service contract (combat, unknown, dead, busy, no-bot).
    CHECK(std::string(ai::PortalRefusalCode(ai::PortalRefusal::InCombat)) == "combat");
    CHECK(std::string(ai::PortalRefusalCode(ai::PortalRefusal::Dead)) == "dead");
    CHECK(std::string(ai::PortalRefusalCode(ai::PortalRefusal::Casting)) == "busy");
    CHECK(std::string(ai::PortalRefusalText(ai::PortalRefusal::Casting)) == "The bot is busy casting.");
    CHECK(std::string(ai::PortalRefusalCode(ai::PortalRefusal::NotLearned)) == "unknown");
    CHECK(std::string(ai::PortalRefusalCode(ai::PortalRefusal::UnknownCity)) == "unknown");

    // Service formatting: table order, spellId:City pairs, "-" when none.
    CHECK(ai::FormatServicePortals({11416, 10059}) == "10059:Stormwind,11416:Ironforge");
    CHECK(ai::FormatServicePortals({}) == "-");
    CHECK(ai::FormatServicePortals({99999}) == "-");

    // City list for the bare whisper.
    CHECK(ai::FormatPortalCityList({"Ironforge", "Stormwind"}) == "Portals: Ironforge, Stormwind");
    CHECK(std::string(ai::PortalCityName(11416)) == "Ironforge");
    CHECK(std::string(ai::PortalCityName(12345)) == "");

    // Rune item id is Rune of Portals.
    CHECK(ai::PortalRuneItemId() == 17032);

    std::puts("  [PASS] portal city matching, refusal order, service formatting");
    return 0;
}
