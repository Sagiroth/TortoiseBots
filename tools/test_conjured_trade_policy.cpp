// Conjured trade-service rank choice: best known Conjure/Healthstone rank
// capped by trader level, improved variants, healthstone detection.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_conjured_trade_policy.cpp -o /tmp/test_conjured_trade
//   /tmp/test_conjured_trade

#include "../ai/playerbot/policy/ConjuredTradePolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsHealthstoneItem;
using ai::IsConjuredServiceItem;
using ai::PickConjuredRank;
using ai::PickHealthstoneItem;
using ai::kConjureFoodRanks;
using ai::kConjureWaterRanks;

static uint32_t const kFoodCount = 7;
static uint32_t const kWaterCount = 7;

int main()
{
    std::cout << "Starting conjured trade policy tests...\n";

    // Mage 60 knows all ranks, trader 30: food rank 4 (Pumpernickel 1487),
    // water rank 4 (Spring Water 3772).
    std::set<uint32_t> all = { 587, 597, 990, 6129, 10144, 10145, 28612,
        5504, 5505, 5506, 6127, 10138, 10139, 10140 };
    auto knows = [&](uint32_t id) { return all.count(id) != 0; };
    ai::ConjuredRankRow const* food = PickConjuredRank(kConjureFoodRanks, kFoodCount, knows, 30);
    ai::ConjuredRankRow const* water = PickConjuredRank(kConjureWaterRanks, kWaterCount, knows, 30);
    CHECK(food && food->itemId == 1487);
    CHECK(water && water->itemId == 3772);
    std::cout << "  [PASS] trader-30 cap picks rank 4 food and water\n";

    // Highest rank wins when the trader can use everything.
    food = PickConjuredRank(kConjureFoodRanks, kFoodCount, knows, 60);
    water = PickConjuredRank(kConjureWaterRanks, kWaterCount, knows, 60);
    CHECK(food && food->itemId == 22895);
    CHECK(IsConjuredServiceItem(22895) && IsConjuredServiceItem(8079) && IsConjuredServiceItem(19013));
    CHECK(!IsConjuredServiceItem(2996) && !IsConjuredServiceItem(0));
    CHECK(water && water->itemId == 8079);
    std::cout << "  [PASS] trader-60 gets the top rank\n";

    // Only rank 1 known: rank 1 even for a high trader.
    auto rank1 = [](uint32_t id) { return id == 587 || id == 5504; };
    food = PickConjuredRank(kConjureFoodRanks, kFoodCount, rank1, 60);
    water = PickConjuredRank(kConjureWaterRanks, kWaterCount, rank1, 60);
    CHECK(food && food->itemId == 5349);
    CHECK(water && water->itemId == 5350);
    std::cout << "  [PASS] single known rank is picked\n";

    // Nothing known, or nothing usable at the trader's level: no goods.
    auto none = [](uint32_t) { return false; };
    CHECK(!PickConjuredRank(kConjureFoodRanks, kFoodCount, none, 60));
    CHECK(!PickConjuredRank(kConjureWaterRanks, kWaterCount, none, 60));
    CHECK(!PickConjuredRank(kConjureFoodRanks, kFoodCount, knows, 0));
    std::cout << "  [PASS] unknown or unusable ranks give nothing\n";

    // Gap in the spellbook: the best known usable rank wins, not the
    // highest learned regardless of trader level.
    auto gap = [](uint32_t id) { return id == 587 || id == 6129; };
    food = PickConjuredRank(kConjureFoodRanks, kFoodCount, gap, 20);
    CHECK(food && food->itemId == 5349);
    food = PickConjuredRank(kConjureFoodRanks, kFoodCount, gap, 30);
    CHECK(food && food->itemId == 1487);
    std::cout << "  [PASS] gaps fall back to the best known usable rank\n";

    // Warlock: best known Create Healthstone rank capped by trader level.
    std::set<uint32_t> locks = { 6201, 6202, 5699, 11729, 11730 };
    auto lockKnows = [&](uint32_t id) { return locks.count(id) != 0; };
    CHECK(PickHealthstoneItem(lockKnows, 60, false, false) == 9421);
    CHECK(PickHealthstoneItem(lockKnows, 30, false, false) == 5509);
    CHECK(PickHealthstoneItem(lockKnows, 10, false, false) == 5512);
    CHECK(PickHealthstoneItem(none, 60, false, false) == 0);
    CHECK(PickHealthstoneItem(lockKnows, 0, false, false) == 0);
    std::cout << "  [PASS] healthstone rank follows the same cap\n";

    // Improved Healthstone picks the matching variant of the same row.
    CHECK(PickHealthstoneItem(lockKnows, 60, true, false) == 19012);
    CHECK(PickHealthstoneItem(lockKnows, 60, false, true) == 19013);
    CHECK(PickHealthstoneItem(lockKnows, 30, true, false) == 19008);
    std::cout << "  [PASS] improved talent picks the row variant\n";

    // Healthstone detection covers every rank and variant.
    for (uint32_t id : { 5512u, 5511u, 5509u, 5510u, 9421u, 19004u, 19005u,
        19006u, 19007u, 19008u, 19009u, 19010u, 19011u, 19012u, 19013u })
        CHECK(IsHealthstoneItem(id));
    CHECK(!IsHealthstoneItem(5349));
    CHECK(!IsHealthstoneItem(3772));
    CHECK(!IsHealthstoneItem(0));
    std::cout << "  [PASS] any healthstone rank or variant is detected\n";

    std::cout << "conjured trade policy: OK\n";
    return 0;
}
