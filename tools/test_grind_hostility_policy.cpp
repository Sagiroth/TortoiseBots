#include "../ai/playerbot/GrindSpotPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::GRIND_UNATTACKABLE_UNIT_FLAGS;
using ai::GrindHostilityAllowed;

namespace
{
    // NPC flags as in the core SharedDefines.h (only the bits the rule reads).
    std::uint32_t const NPC_NONE = 0;
    std::uint32_t const NPC_VENDOR = 0x80;   // UNIT_NPC_FLAG_VENDOR
    std::uint32_t const NPC_TRAINER = 0x10;  // UNIT_NPC_FLAG_TRAINER

    // Unit-flag bits as in the core UnitDefines.h. Values, not names: the
    // policy header is a dependency-free rule the standalone test includes
    // without the core.
    std::uint32_t const UNIT_FLAG_NOT_ATTACKABLE_1 = 0x00000080u;
    std::uint32_t const UNIT_FLAG_IMMUNE_TO_PLAYER = 0x00000100u;
    std::uint32_t const UNIT_FLAG_IMMUNE_TO_NPC = 0x00000200u;
    std::uint32_t const UNIT_FLAG_NON_ATTACKABLE_2 = 0x00010000u;
    std::uint32_t const UNIT_FLAG_NOT_SELECTABLE = 0x02000000u;

    // Creature ranks as in the core SharedDefines.h.
    std::uint32_t const RANK_NORMAL = 0; // CREATURE_ELITE_NORMAL
    std::uint32_t const RANK_ELITE = 1;  // CREATURE_ELITE_ELITE
    std::uint32_t const RANK_RARE = 4;   // CREATURE_ELITE_RARE

    // The measured issue-#393 case: a level 1-3 bot's starter beasts.
    // Thistle Boar (faction 189) / Young Nightsaber (faction 7) read
    // REP_NEUTRAL against a player faction template, carry no NPC flag and
    // pay XP (xp_multiplier 1); their factions have no reputation list, no
    // unattackable unit bits, rank 0 (tw_world verified).
    bool Boar()
    {
        return GrindHostilityAllowed(false, false, NPC_NONE, true, true, 0, RANK_NORMAL);
    }
}

int main()
{
    std::cout << "Starting TortoiseBots grind-hostility regression tests...\n";

    // -------------------------------------------------------------
    // Test 1: neutral starter wildlife is prey (issue #393)
    // -------------------------------------------------------------
    {
        CHECK(Boar());
        // Coyote/wolf equivalents in the other starter valleys: same shape
        // (faction 189/7/22/32/66, reputation_list_id -1, flags 0, rank 0).
        CHECK(GrindHostilityAllowed(false, false, NPC_NONE, true, true, 0, RANK_NORMAL));
        std::cout << "  [PASS] neutral XP-paying wildlife is grind prey\n";
    }

    // -------------------------------------------------------------
    // Test 2: hostile entries stay prey, whatever they are
    // -------------------------------------------------------------
    {
        CHECK(GrindHostilityAllowed(true, false, NPC_NONE, true, true, 0, RANK_NORMAL));
        CHECK(GrindHostilityAllowed(true, false, NPC_VENDOR, true, false, 0, RANK_ELITE));
        CHECK(GrindHostilityAllowed(true, false, NPC_NONE, false, false, UNIT_FLAG_NOT_SELECTABLE, RANK_RARE));
        std::cout << "  [PASS] hostile entries are always prey\n";
    }

    // -------------------------------------------------------------
    // Test 3: friendly entries are never prey
    // -------------------------------------------------------------
    {
        CHECK(!GrindHostilityAllowed(false, true, NPC_NONE, true, true, 0, RANK_NORMAL));
        CHECK(!GrindHostilityAllowed(false, true, NPC_VENDOR, true, true, 0, RANK_NORMAL));
        // Hostile wins over friendly when both read true (cannot happen
        // through the core reaction, but the order must be deterministic).
        CHECK(GrindHostilityAllowed(true, true, NPC_NONE, true, true, 0, RANK_NORMAL));
        std::cout << "  [PASS] friendly entries are never prey\n";
    }

    // -------------------------------------------------------------
    // Test 4: neutral reputation-faction NPCs are never prey (review)
    // -------------------------------------------------------------
    {
        // Steamwheedle bruisers (Ratchet 3502, Everlook 11190, Booty Bay 4624),
        // Cenarion infantry/outriders (15184, 15545), Dalaran casters (1867,
        // 1912, 1914): core IsValidAttackTarget refuses neutral-rep targets
        // without AT_WAR, so the bot would arrive and stand.
        CHECK(!GrindHostilityAllowed(false, false, NPC_NONE, true, false, 0, RANK_NORMAL));
        CHECK(!GrindHostilityAllowed(false, false, NPC_NONE, true, false, 0, RANK_ELITE));
        std::cout << "  [PASS] neutral reputation-faction entries are never prey\n";
    }

    // -------------------------------------------------------------
    // Test 5: neutral unattackable-flagged entries are never prey (review)
    // -------------------------------------------------------------
    {
        // The mask mirrors core IsTargetable(forAttack, isAttackerPlayer):
        // each bit refuses the attack on its own. Verified against live
        // tw_world rows (Theramore dummy 4952, Archery Target 5202, Practice
        // Dummy 5652, Redrock/Fire/Corrupt spirits 5890/5896/5897).
        CHECK((GRIND_UNATTACKABLE_UNIT_FLAGS & UNIT_FLAG_NOT_ATTACKABLE_1) != 0);
        CHECK((GRIND_UNATTACKABLE_UNIT_FLAGS & UNIT_FLAG_NOT_SELECTABLE) != 0);
        CHECK((GRIND_UNATTACKABLE_UNIT_FLAGS & UNIT_FLAG_IMMUNE_TO_PLAYER) != 0);
        CHECK((GRIND_UNATTACKABLE_UNIT_FLAGS & UNIT_FLAG_NON_ATTACKABLE_2) != 0);
        CHECK(!GrindHostilityAllowed(false, false, NPC_NONE, true, true, UNIT_FLAG_NOT_ATTACKABLE_1, RANK_NORMAL));
        CHECK(!GrindHostilityAllowed(false, false, NPC_NONE, true, true, UNIT_FLAG_IMMUNE_TO_PLAYER, RANK_NORMAL));
        CHECK(!GrindHostilityAllowed(false, false, NPC_NONE, true, true, UNIT_FLAG_NOT_SELECTABLE, RANK_NORMAL));
        CHECK(!GrindHostilityAllowed(false, false, NPC_NONE, true, true, UNIT_FLAG_NON_ATTACKABLE_2, RANK_NORMAL));
        CHECK(!GrindHostilityAllowed(false, false, NPC_NONE, true, true,
            UNIT_FLAG_NOT_ATTACKABLE_1 | UNIT_FLAG_IMMUNE_TO_PLAYER, RANK_NORMAL));
        // IMMUNE_TO_NPC alone does not refuse a player attacker (core only
        // reads it for NPC attackers), so it must not exclude: 107 spawned
        // entries carry it and stay prey.
        CHECK(GrindHostilityAllowed(false, false, NPC_NONE, true, true, UNIT_FLAG_IMMUNE_TO_NPC, RANK_NORMAL));
        std::cout << "  [PASS] neutral unattackable entries are never prey\n";
    }

    // -------------------------------------------------------------
    // Test 6: neutral elites/rares/bosses are never prey (review)
    // -------------------------------------------------------------
    {
        // Booty Bay Bruiser (4624, elite), Threggil (14432, rare),
        // Githyiss (1994, rare): the bot walks past, it does not solo them.
        CHECK(!GrindHostilityAllowed(false, false, NPC_NONE, true, true, 0, RANK_ELITE));
        CHECK(!GrindHostilityAllowed(false, false, NPC_NONE, true, true, 0, RANK_RARE));
        CHECK(!GrindHostilityAllowed(false, false, NPC_NONE, true, true, 0, 2));
        CHECK(!GrindHostilityAllowed(false, false, NPC_NONE, true, true, 0, 3));
        std::cout << "  [PASS] neutral elites/rares/bosses are never prey\n";
    }

    // -------------------------------------------------------------
    // Test 7: neutral service mobs are never prey
    // -------------------------------------------------------------
    {
        // A neutral vendor / trainer must not become a grind destination
        // just because it is not hostile.
        CHECK(!GrindHostilityAllowed(false, false, NPC_VENDOR, true, true, 0, RANK_NORMAL));
        CHECK(!GrindHostilityAllowed(false, false, NPC_TRAINER, true, true, 0, RANK_NORMAL));
        CHECK(!GrindHostilityAllowed(false, false, NPC_VENDOR | NPC_TRAINER, false, true, 0, RANK_NORMAL));
        std::cout << "  [PASS] neutral NPCs are never prey\n";
    }

    // -------------------------------------------------------------
    // Test 8: neutral no-XP creatures are never prey
    // -------------------------------------------------------------
    {
        // Critters (xp_multiplier 0), Deer / Toads: walking to them buys
        // no XP, so they are not destinations.
        CHECK(!GrindHostilityAllowed(false, false, NPC_NONE, false, true, 0, RANK_NORMAL));
        std::cout << "  [PASS] neutral no-XP creatures are never prey\n";
    }

    std::cout << "All grind-hostility tests passed.\n";
    return 0;
}
