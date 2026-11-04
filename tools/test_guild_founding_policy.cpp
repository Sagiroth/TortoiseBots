// Standalone test for ai/playerbot/policy/guild/GuildFoundingPolicy.h: the
// charter price/level math and the buy/turn-in trip predicates shared by the
// petition travel need, the buy gate and the turn-in gates. No server headers.
#include "../ai/playerbot/policy/guild/GuildFoundingPolicy.h"

#include <cstdint>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        return 1; \
    } \
} while (0)

int main()
{
    using namespace ai;

    CHECK(GUILD_CHARTER_ITEM_ID == 5863);
    CHECK(GUILD_CHARTER_COST_COPPER == 1000);
    CHECK(GUILD_CHARTER_MIN_LEVEL == 10);

    // Level gate mirrors the budget rule: no charter money reserved at 10 and
    // below, so no trip either.
    CHECK(!GuildCharterLevelEligible(1));
    CHECK(!GuildCharterLevelEligible(10));
    CHECK(GuildCharterLevelEligible(11));
    CHECK(GuildCharterLevelEligible(60));

    CHECK(!GuildCharterAffordable(0));
    CHECK(!GuildCharterAffordable(999));
    CHECK(GuildCharterAffordable(1000));
    CHECK(GuildCharterAffordable(50000));

    // Turn-in needs the core's count (default 9); fewer signs keep gathering.
    CHECK(!GuildPetitionReadyToTurnIn(0, 9));
    CHECK(!GuildPetitionReadyToTurnIn(8, 9));
    CHECK(GuildPetitionReadyToTurnIn(9, 9));
    CHECK(GuildPetitionReadyToTurnIn(10, 9));

    // Buy leg: unguilded, no charter yet, level and purse allow it.
    CHECK(GuildFoundingBuyTripNeeded(false, false, 60, 50000));
    CHECK(!GuildFoundingBuyTripNeeded(true, false, 60, 50000));
    CHECK(!GuildFoundingBuyTripNeeded(false, true, 60, 50000));
    CHECK(!GuildFoundingBuyTripNeeded(false, false, 10, 50000));
    CHECK(!GuildFoundingBuyTripNeeded(false, false, 60, 999));

    // Turn-in leg: unguilded, charter aboard, signatures complete.
    CHECK(GuildFoundingTurnInTripNeeded(false, true, 9, 9));
    CHECK(!GuildFoundingTurnInTripNeeded(true, true, 9, 9));
    CHECK(!GuildFoundingTurnInTripNeeded(false, false, 9, 9));
    CHECK(!GuildFoundingTurnInTripNeeded(false, true, 8, 9));

    std::cout << "guild founding policy: OK\n";
    return 0;
}
