#include "../ai/playerbot/GrindSpotPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::GrindHostilityAllowed;

namespace
{
    // NPC flags as in the core SharedDefines.h (only the bits the rule reads).
    std::uint32_t const NPC_NONE = 0;
    std::uint32_t const NPC_VENDOR = 0x80;   // UNIT_NPC_FLAG_VENDOR
    std::uint32_t const NPC_TRAINER = 0x10;  // UNIT_NPC_FLAG_TRAINER

    // The measured issue-#393 case: a level 1-3 bot's starter beasts.
    // Thistle Boar (faction 189) / Young Nightsaber (faction 7) read
    // REP_NEUTRAL against a player faction template, carry no NPC flag and
    // pay XP (xp_multiplier 1).
    bool Boar() { return GrindHostilityAllowed(false, false, NPC_NONE, true); }
}

int main()
{
    std::cout << "Starting TortoiseBots grind-hostility regression tests...\n";

    // -------------------------------------------------------------
    // Test 1: neutral starter wildlife is prey (issue #393)
    // -------------------------------------------------------------
    {
        CHECK(Boar());
        // Coyote/wolf equivalents in the other starter valleys: same shape.
        CHECK(GrindHostilityAllowed(false, false, NPC_NONE, true));
        std::cout << "  [PASS] neutral XP-paying wildlife is grind prey\n";
    }

    // -------------------------------------------------------------
    // Test 2: hostile entries stay prey, whatever they are
    // -------------------------------------------------------------
    {
        CHECK(GrindHostilityAllowed(true, false, NPC_NONE, true));
        CHECK(GrindHostilityAllowed(true, false, NPC_VENDOR, true));
        CHECK(GrindHostilityAllowed(true, false, NPC_NONE, false));
        std::cout << "  [PASS] hostile entries are always prey\n";
    }

    // -------------------------------------------------------------
    // Test 3: friendly entries are never prey
    // -------------------------------------------------------------
    {
        CHECK(!GrindHostilityAllowed(false, true, NPC_NONE, true));
        CHECK(!GrindHostilityAllowed(false, true, NPC_VENDOR, true));
        // Hostile wins over friendly when both read true (cannot happen
        // through the core reaction, but the order must be deterministic).
        CHECK(GrindHostilityAllowed(true, true, NPC_NONE, true));
        std::cout << "  [PASS] friendly entries are never prey\n";
    }

    // -------------------------------------------------------------
    // Test 4: neutral service mobs are never prey
    // -------------------------------------------------------------
    {
        // A neutral vendor / trainer must not become a grind destination
        // just because it is not hostile.
        CHECK(!GrindHostilityAllowed(false, false, NPC_VENDOR, true));
        CHECK(!GrindHostilityAllowed(false, false, NPC_TRAINER, true));
        CHECK(!GrindHostilityAllowed(false, false, NPC_VENDOR | NPC_TRAINER, false));
        std::cout << "  [PASS] neutral NPCs are never prey\n";
    }

    // -------------------------------------------------------------
    // Test 5: neutral no-XP creatures are never prey
    // -------------------------------------------------------------
    {
        // Critters (xp_multiplier 0), Deer / Toads: walking to them buys
        // no XP, so they are not destinations.
        CHECK(!GrindHostilityAllowed(false, false, NPC_NONE, false));
        std::cout << "  [PASS] neutral no-XP creatures are never prey\n";
    }

    std::cout << "All grind-hostility tests passed.\n";
    return 0;
}
