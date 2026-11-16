// Standalone regression test for AH bot item pool filtering, proportions,
// and weighted selection (runtime/policy/ah/AhBotItemPoolPolicy.h).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_ahbot_item_pool_policy.cpp -o /tmp/test_ahbot_item_pool_policy && /tmp/test_ahbot_item_pool_policy

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "../runtime/policy/ah/AhBotItemPoolPolicy.h"

namespace
{

int g_checks = 0;

#define CHECK(cond)                                                              \
    do                                                                           \
    {                                                                            \
        ++g_checks;                                                              \
        if (!(cond))                                                             \
        {                                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

ai::AhBotItemFacts MakeValidItem(uint32_t id, uint32_t cls, uint32_t qual)
{
    ai::AhBotItemFacts f;
    f.itemId = id;
    f.itemClass = cls;
    f.itemSubClass = 0;
    f.name = "Valid Sample Item";
    f.quality = qual;
    f.buyPrice = 1000;
    f.sellPrice = 250;
    f.itemLevel = 20;
    f.requiredLevel = 15;
    f.containerSlots = (cls == ai::kAhBotClassContainer) ? 10 : 0;
    f.bonding = 0;
    f.isObtainable = true;
    return f;
}

void TestCoreInvariants()
{
    ai::AhBotItemFilterConfig cfg;

    // Zero itemId
    auto f = MakeValidItem(0, ai::kAhBotClassArmor, ai::kAhBotQualityUncommon);
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));

    // No greys ever (quality 0), even if proportion > 0 or exception ID
    f = MakeValidItem(100, ai::kAhBotClassArmor, ai::kAhBotQualityPoor);
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));
    f = MakeValidItem(11732, ai::kAhBotClassQuest, ai::kAhBotQualityPoor);
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));

    // Quality > Artifact rejected
    f = MakeValidItem(101, ai::kAhBotClassArmor, 7);
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));

    // BoP, quest-bound, account-bound rejected (BoE=2, BoU=3, NoBind=0 allowed)
    for (uint32_t bond : { ai::kAhBotBindWhenPickedUp, ai::kAhBotBindQuestItem,
                           ai::kAhBotBindQuestItem1, ai::kAhBotBindAccount })
    {
        f = MakeValidItem(102, ai::kAhBotClassWeapon, ai::kAhBotQualityRare);
        f.bonding = bond;
        CHECK(!ai::AhBotIsItemCandidate(f, cfg));
    }
    for (uint32_t bond : { 0u, 2u, 3u })
    {
        f = MakeValidItem(103, ai::kAhBotClassWeapon, ai::kAhBotQualityRare);
        f.bonding = bond;
        CHECK(ai::AhBotIsItemCandidate(f, cfg));
    }

    // Unobtainable or unlimited vendor stock rejected
    f = MakeValidItem(104, ai::kAhBotClassTradeGoods, ai::kAhBotQualityNormal);
    f.isObtainable = false;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));

    f = MakeValidItem(105, ai::kAhBotClassTradeGoods, ai::kAhBotQualityNormal);
    f.isSoldByUnlimitedVendor = true;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));

    // ExtraFlags ITEM_EXTRA_NOT_OBTAINABLE rejected
    f = MakeValidItem(106, ai::kAhBotClassArmor, ai::kAhBotQualityUncommon);
    f.extraFlags = ai::kAhBotItemExtraNotObtainable;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));

    // Transmog clone rejected
    f = MakeValidItem(107, ai::kAhBotClassArmor, ai::kAhBotQualityEpic);
    f.isTransmogClone = true;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));

    // Non-vanilla / excluded classes rejected (Gem=3, Money=10, Permanent=14)
    for (uint32_t badClass : { ai::kAhBotClassGem, ai::kAhBotClassMoney, ai::kAhBotClassPermanent, 16u })
    {
        f = MakeValidItem(108, badClass, ai::kAhBotQualityUncommon);
        CHECK(!ai::AhBotIsItemCandidate(f, cfg));
        CHECK(ai::AhBotCategoryName(badClass) == nullptr);
    }

    // All 13 vanilla categories accepted
    for (uint32_t cls : { 0u, 1u, 2u, 4u, 5u, 6u, 7u, 8u, 9u, 11u, 12u, 13u, 15u })
    {
        CHECK(ai::AhBotCategoryName(cls) != nullptr);
        f = MakeValidItem(200 + cls, cls, ai::kAhBotQualityUncommon);
        CHECK(ai::AhBotIsItemCandidate(f, cfg));
    }
}

void TestContentAndPriceFilters()
{
    ai::AhBotItemFilterConfig cfg;

    // Conjured
    auto f = MakeValidItem(300, ai::kAhBotClassConsumable, ai::kAhBotQualityNormal);
    f.flags = ai::kAhBotItemFlagConjured;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));

    // Money loot
    f = MakeValidItem(301, ai::kAhBotClassMisc, ai::kAhBotQualityNormal);
    f.minMoneyLoot = 10;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));

    // Duration
    f = MakeValidItem(302, ai::kAhBotClassConsumable, ai::kAhBotQualityNormal);
    f.duration = 3600;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));

    // 0-slot container
    f = MakeValidItem(303, ai::kAhBotClassContainer, ai::kAhBotQualityNormal);
    f.containerSlots = 0;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));

    // White recipe book excluded, green recipe book allowed, white non-book recipe allowed
    f = MakeValidItem(304, ai::kAhBotClassRecipe, ai::kAhBotQualityNormal);
    f.itemSubClass = ai::kAhBotSubclassRecipeBook;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));
    f.quality = ai::kAhBotQualityUncommon;
    CHECK(ai::AhBotIsItemCandidate(f, cfg));
    f.quality = ai::kAhBotQualityNormal;
    f.itemSubClass = 5; // cooking recipe
    CHECK(ai::AhBotIsItemCandidate(f, cfg));

    // Text filter
    for (char const* badName : { "Test Sword", "TEST Item", "Deprecated Ring", "Depricated Belt",
                                 "An Epic Sword", "Item]", "D'Sak Blade", "Sword (Old)",
                                 "OLD Chest", "PVP Helm" })
    {
        f = MakeValidItem(305, ai::kAhBotClassWeapon, ai::kAhBotQualityRare);
        f.name = badName;
        CHECK(!ai::AhBotIsItemCandidate(f, cfg));
    }
    cfg.disabledItemTextFilter = false;
    f = MakeValidItem(305, ai::kAhBotClassWeapon, ai::kAhBotQualityRare);
    f.name = "OLD Chest";
    CHECK(ai::AhBotIsItemCandidate(f, cfg));
    cfg.disabledItemTextFilter = true;

    // No-price items excluded except item enhancements, enchanting trade goods, and Libram/Diamond exceptions
    f = MakeValidItem(306, ai::kAhBotClassMisc, ai::kAhBotQualityNormal);
    f.buyPrice = 0;
    f.sellPrice = 0;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));

    f = MakeValidItem(307, ai::kAhBotClassConsumable, ai::kAhBotQualityNormal);
    f.itemSubClass = ai::kAhBotSubclassConsumableEnhancement;
    f.buyPrice = 0;
    f.sellPrice = 0;
    CHECK(ai::AhBotIsItemCandidate(f, cfg));

    f = MakeValidItem(308, ai::kAhBotClassTradeGoods, ai::kAhBotQualityUncommon);
    f.itemSubClass = ai::kAhBotSubclassTradeGoodsEnchanting;
    f.buyPrice = 0;
    f.sellPrice = 0;
    CHECK(ai::AhBotIsItemCandidate(f, cfg));

    for (uint32_t exId : { 11732u, 11733u, 11734u, 11736u, 11737u, 18332u, 18333u, 18334u, 18335u })
    {
        f = MakeValidItem(exId, ai::kAhBotClassRecipe, ai::kAhBotQualityUncommon);
        f.itemSubClass = ai::kAhBotSubclassRecipeBook;
        f.buyPrice = 0;
        f.sellPrice = 0;
        CHECK(ai::AhBotIsItemCandidate(f, cfg));
    }
}

void TestRestrictionsAndParsing()
{
    ai::AhBotItemFilterConfig cfg;

    // Default equip level restrict caps RequiredLevel at 60 while allowing RequiredLevel == 0
    auto f = MakeValidItem(400, ai::kAhBotClassArmor, ai::kAhBotQualityRare);
    f.requiredLevel = 61;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));
    f.requiredLevel = 60;
    CHECK(ai::AhBotIsItemCandidate(f, cfg));
    f.requiredLevel = 0;
    CHECK(ai::AhBotIsItemCandidate(f, cfg));

    // Equip level exception
    cfg.equipLevelExceptions.insert(400);
    f.requiredLevel = 65;
    CHECK(ai::AhBotIsItemCandidate(f, cfg));

    // Item level restriction with UseCraftedItemForItemLevel
    cfg.itemLevelRestrictEnabled = true;
    cfg.minItemLevel = 10;
    cfg.maxItemLevel = 40;
    cfg.useCraftedItemForItemLevel = true;
    f = MakeValidItem(401, ai::kAhBotClassRecipe, ai::kAhBotQualityUncommon);
    f.itemSubClass = 4;
    f.itemLevel = 20;
    f.hasProducedItem = true;
    f.producedItemLevel = 50;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg)); // uses produced item level 50 > 40
    cfg.useCraftedItemForItemLevel = false;
    CHECK(ai::AhBotIsItemCandidate(f, cfg));  // uses recipe's own item level 20
    cfg.itemLevelExceptions.insert(401);
    cfg.useCraftedItemForItemLevel = true;
    CHECK(ai::AhBotIsItemCandidate(f, cfg));
    cfg.itemLevelRestrictEnabled = false;

    // Item ID restriction + exception
    cfg.itemIdRestrictEnabled = true;
    cfg.minItemId = 1000;
    cfg.maxItemId = 2000;
    f = MakeValidItem(500, ai::kAhBotClassWeapon, ai::kAhBotQualityUncommon);
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));
    cfg.itemIdExceptions.insert(500);
    CHECK(ai::AhBotIsItemCandidate(f, cfg));
    cfg.itemIdRestrictEnabled = false;

    // Number list parsing with ranges and malformed tokens
    std::set<uint32_t> parsed;
    ai::AhBotParseNumberListToSet(parsed, " 10 , 20-23, 50-40, abc, 0, 99 ");
    CHECK(parsed.size() == 6);
    CHECK(parsed.count(10) == 1);
    CHECK(parsed.count(20) == 1 && parsed.count(21) == 1 && parsed.count(22) == 1 && parsed.count(23) == 1);
    CHECK(parsed.count(99) == 1);
    CHECK(parsed.count(0) == 0);
    CHECK(parsed.count(45) == 0);

    cfg.disabledItems = parsed;
    f = MakeValidItem(22, ai::kAhBotClassArmor, ai::kAhBotQualityUncommon);
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));

    // DisabledRecipeProducedItemClassSubClasses
    cfg.disabledRecipeProducedFilterEnabled = true;
    ai::AhBotParseClassSubClassMap(cfg.disabledRecipeProducedClassSubClasses, "2:*,4:*,15:2,15:5");
    f = MakeValidItem(402, ai::kAhBotClassArmor, ai::kAhBotQualityUncommon);
    f.itemSubClass = 3;
    f.isProducedByRecipe = true;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));
    f.isProducedByRecipe = false;
    CHECK(ai::AhBotIsItemCandidate(f, cfg)); // non-crafted armor not blocked
    f = MakeValidItem(403, ai::kAhBotClassMisc, ai::kAhBotQualityRare);
    f.itemSubClass = 5;
    f.isProducedByRecipe = true;
    CHECK(!ai::AhBotIsItemCandidate(f, cfg));
    f.itemSubClass = 1;
    CHECK(ai::AhBotIsItemCandidate(f, cfg));
}

void TestProportionsAndMultipliers()
{
    ai::AhBotItemPoolTable table;
    std::array<std::array<uint32_t, ai::kAhBotMaxItemQuality>, ai::kAhBotMaxItemClass> props{};
    for (uint32_t c = 0; c < ai::kAhBotMaxItemClass; ++c)
        for (uint32_t q = 0; q < ai::kAhBotMaxItemQuality; ++q)
            props[c][q] = ai::AhBotDefaultListProportion(c, q);

    // Empty table returns 0
    std::unordered_map<uint32_t, uint32_t> mults;
    ai::AhBotParseItemValuePairs(mults, "2589:10,0:5,100:0,bad:2");
    CHECK(mults.size() == 1 && mults[2589] == 10);

    table.BuildProportions(props, mults);
    CHECK(table.Pick([](uint64_t) { return 0ULL; }) == 0);
    CHECK(table.CandidateCount() == 0);

    // Add TradeGood Normal (2589 multiplied 10x, 2590 weight 1x) and Weapon Uncommon (3001 weight 1x)
    // Proportions: TradeGood Normal = 220, Weapon Uncommon = 35 between categories.
    // Also add Armor Poor (if someone tried) - note pool policy already blocks Poor, and here we
    // also test that empty buckets with non-zero proportions are skipped.
    table.AddCandidate(2589, ai::kAhBotClassTradeGoods, ai::kAhBotQualityNormal);
    table.AddCandidate(2590, ai::kAhBotClassTradeGoods, ai::kAhBotQualityNormal);
    table.AddCandidate(3001, ai::kAhBotClassWeapon, ai::kAhBotQualityUncommon);
    // Duplicate add must be ignored
    table.AddCandidate(2589, ai::kAhBotClassTradeGoods, ai::kAhBotQualityNormal);
    table.BuildProportions(props, mults);

    CHECK(table.CandidateCount() == 3);
    CHECK(table.IsCandidate(2589));
    CHECK(table.IsCandidate(2590));
    CHECK(table.IsCandidate(3001));
    CHECK(!table.IsCandidate(9999));
    CHECK(table.TotalProportionWeight() == 255); // 220 + 35; empty buckets excluded

    std::mt19937_64 rng(42);
    auto nextRand = [&rng](uint64_t limit) -> uint64_t {
        std::uniform_int_distribution<uint64_t> dist(0, limit - 1);
        return dist(rng);
    };

    uint64_t c2589 = 0, c2590 = 0, c3001 = 0;
    constexpr int kTrials = 120000;
    for (int i = 0; i < kTrials; ++i)
    {
        uint32_t picked = table.Pick(nextRand);
        if (picked == 2589)
            ++c2589;
        else if (picked == 2590)
            ++c2590;
        else if (picked == 3001)
            ++c3001;
        else
            CHECK(false);
    }

    // Category ratio: TradeGoods (2589+2590) vs Weapon (3001) follows 220 vs 35
    // despite 2589 having a 10x intra-bucket multiplier.
    double const tgShare = static_cast<double>(c2589 + c2590) / kTrials;
    double const wpnShare = static_cast<double>(c3001) / kTrials;
    CHECK(tgShare > 0.85 && tgShare < 0.875);
    CHECK(wpnShare > 0.125 && wpnShare < 0.15);

    // Intra-bucket multiplier: 2589 should be ~10x more frequent than 2590
    double const ratio = static_cast<double>(c2589) / static_cast<double>(c2590);
    CHECK(ratio > 9.0 && ratio < 11.0);

    table.SetPhaseStats(2, 17);
    std::string const status = table.GetStatus();
    CHECK(status.find("candidates=3") != std::string::npos);
    CHECK(status.find("phase=2") != std::string::npos);
    CHECK(status.find("lockedByPhase=17") != std::string::npos);
    CHECK(status.find("TradeGood") != std::string::npos);
    CHECK(status.find("Weapon") != std::string::npos);

    // Zero proportions yield 0 even when candidates exist
    std::array<std::array<uint32_t, ai::kAhBotMaxItemQuality>, ai::kAhBotMaxItemClass> zeroProps{};
    table.BuildProportions(zeroProps, mults);
    CHECK(table.TotalProportionWeight() == 0);
    CHECK(table.Pick(nextRand) == 0);
}

void TestContentPhaseRules()
{
    // Source phase combination: empty/no-spawn -> 0, world+raid -> 0, raid-only -> min raid phase
    CHECK(ai::AhBotCombineSourcePhases({}) == 0);
    CHECK(ai::AhBotCombineSourcePhases({ 1 }) == 1);
    CHECK(ai::AhBotCombineSourcePhases({ 0, 1 }) == 0);
    CHECK(ai::AhBotCombineSourcePhases({ 2, 1, 3 }) == 1);

    // Crafted item phase combination
    // 1) Recipe only in ZG (phase 1) with phase-0 reagents -> crafted item phase 1
    CHECK(ai::AhBotCombineCraftedPhase(1, { 0, 0 }) == 1);
    // 2) Trainer recipe (learnPhase 0) with an Elementium reagent (phase 1) -> phase 1
    CHECK(ai::AhBotCombineCraftedPhase(0, { 0, 1 }) == 1);
    // 3) Phase-1 recipe with phase-3 reagent -> phase 3
    CHECK(ai::AhBotCombineCraftedPhase(1, { 3 }) == 3);
    // 4) Trainer recipe with no reagents -> phase 0
    CHECK(ai::AhBotCombineCraftedPhase(0, {}) == 0);

    // Candidate filtering across content phases 1..4 (0..3) and RespectContentPhase toggle
    ai::AhBotItemFilterConfig cfg;
    cfg.respectContentPhase = true;
    cfg.contentPhase = 0; // Phase 1

    auto worldItem = MakeValidItem(700, ai::kAhBotClassTradeGoods, ai::kAhBotQualityNormal);
    worldItem.contentPhase = ai::AhBotCombineSourcePhases({ 0, 1 });
    CHECK(worldItem.contentPhase == 0);
    CHECK(ai::AhBotIsItemCandidate(worldItem, cfg));

    auto bwlItem = MakeValidItem(18562, ai::kAhBotClassTradeGoods, ai::kAhBotQualityEpic);
    bwlItem.contentPhase = ai::AhBotCombineSourcePhases({ 1 });
    CHECK(!ai::AhBotIsItemCandidate(bwlItem, cfg)); // locked at phase 1

    auto naxxItem = MakeValidItem(22682, ai::kAhBotClassTradeGoods, ai::kAhBotQualityRare);
    naxxItem.contentPhase = 3;
    CHECK(!ai::AhBotIsItemCandidate(naxxItem, cfg));

    // Phase 2 unlocks BWL/ZG/ES (phase 1) while keeping AQ/Naxx locked
    cfg.contentPhase = 1;
    CHECK(ai::AhBotIsItemCandidate(bwlItem, cfg));
    CHECK(!ai::AhBotIsItemCandidate(naxxItem, cfg));

    // Phase 4 unlocks everything
    cfg.contentPhase = 3;
    CHECK(ai::AhBotIsItemCandidate(naxxItem, cfg));

    // RespectContentPhase = 0 lists everything even at phase 1
    cfg.contentPhase = 0;
    cfg.respectContentPhase = false;
    CHECK(ai::AhBotIsItemCandidate(bwlItem, cfg));
    CHECK(ai::AhBotIsItemCandidate(naxxItem, cfg));
}

} // namespace

int main()
{
    TestCoreInvariants();
    TestContentAndPriceFilters();
    TestRestrictionsAndParsing();
    TestProportionsAndMultipliers();
    TestContentPhaseRules();
    std::printf("OK (%d checks passed)\n", g_checks);
    return 0;
}
