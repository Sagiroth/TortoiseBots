#include "../ai/playerbot/LootRollPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::LootMethodTakesRolls;
using ai::LootRollVote;
using ai::RecipeRollVote;
using ai::TokenUsableByClass;
using ai::UniqueCopyOwned;

int main()
{
    std::cout << "Starting TortoiseBots loot-roll gate tests...\n";

    // (1) Loot method: free-for-all (0) and master loot (2) take no auto
    // vote (donor passes); group (3) and need-before-greed (4) do.
    {
        CHECK(!LootMethodTakesRolls(0));
        CHECK(!LootMethodTakesRolls(2));
        CHECK(LootMethodTakesRolls(3));
        CHECK(LootMethodTakesRolls(4));
        std::cout << "  [PASS] FFA/master pass, group/NBG vote\n";
    }

    // (2) Recipe: learnable NEEDs, soulbound-unusable PASSes (nobody else
    // can use it), tradeable-unusable GREEDs for the AH.
    {
        CHECK(RecipeRollVote(true, true) == LootRollVote::Need);
        CHECK(RecipeRollVote(true, false) == LootRollVote::Need);
        CHECK(RecipeRollVote(false, true) == LootRollVote::Pass);
        CHECK(RecipeRollVote(false, false) == LootRollVote::Greed);
        std::cout << "  [PASS] recipe NEED/PASS/GREED split\n";
    }

    // (3) Duplicate unique: UNIQUE_EQUIPPED already worn, or a stack at
    // MaxCount, is owned (donor RollUniqueCheck demotes NEED to GREED).
    // Zero MaxCount means no cap; an unworn unique is not owned.
    {
        CHECK(UniqueCopyOwned(true, true, 1, 1));
        CHECK(UniqueCopyOwned(false, false, 5, 5));
        CHECK(UniqueCopyOwned(false, false, 3, 5) == false);
        CHECK(UniqueCopyOwned(false, false, 3, 0) == false);
        CHECK(UniqueCopyOwned(false, true, 0, 0) == false);
        std::cout << "  [PASS] duplicate-unique ownership\n";
    }

    // (4) Class token: NEED only when the bot's class is in the
    // AllowableClass mask; an empty mask is unrestricted (all classes).
    // Warrior mask bit: class 1 -> bit 0.
    {
        const std::uint32_t warrior = 1u << (1 - 1);
        CHECK(TokenUsableByClass(warrior, warrior));
        CHECK(!TokenUsableByClass(warrior << 2, warrior));
        CHECK(TokenUsableByClass(0, warrior));
        std::cout << "  [PASS] token class-mask gate\n";
    }

    std::cout << "All loot-roll gate checks PASSED!\n";
    return 0;
}
