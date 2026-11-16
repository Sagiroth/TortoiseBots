// Standalone policy test for AH bot pricing and stack sizing (issue #1639).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_ahbot_pricing_policy.cpp -o /tmp/test_ahbot_pricing_policy && /tmp/test_ahbot_pricing_policy

#include "../runtime/policy/ah/AhBotPricingPolicy.h"

#include <cstdint>
#include <iostream>

namespace
{
int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; \
        ++failures; \
    } \
} while (0)
} // namespace

using ai::AhBotAdvancedGroup;
using ai::AhBotItemSpec;
using ai::AhBotPricePair;
using ai::AhBotPricingConfig;
using ai::CalculateBuyerFairValuePerUnit;
using ai::CalculateItemValue;
using ai::CalculateListingPrice;
using ai::ClassifyAdvancedGroup;
using ai::GetAdvancedPricingMultiplier;
using ai::GetStackSizeForItem;
using ai::MakeDefaultVanillaPricingConfig;
using ai::ParseItemValuePairs;

int main()
{
    // 1. ParseItemValuePairs
    {
        auto parsed = ParseItemValuePairs(" 2589:100 , 4306:500, bad, 0:10, 123:0, 2589:999, 777:42 ");
        CHECK(parsed.size() == 3);
        CHECK(parsed[2589] == 100); // first wins
        CHECK(parsed[4306] == 500);
        CHECK(parsed[777] == 42);
    }

    // 2. Stack sizing (GetStackSizeForItem)
    {
        AhBotPricingConfig cfg = MakeDefaultVanillaPricingConfig();
        AhBotItemSpec sword{};
        sword.itemClass = ai::kAhBotPriceClassWeapon;
        sword.maxStackSize = 1;
        CHECK(GetStackSizeForItem(sword, cfg, [](uint32_t lo, uint32_t) { return lo; }) == 1);

        AhBotItemSpec linen{};
        linen.itemId = 2589;
        linen.itemClass = ai::kAhBotPriceClassTradeGoods;
        linen.maxStackSize = 20;
        // RandomRatio = 75: roll 0 (<75) triggers stacked, roll 1..4 selects increment multiples of 5.
        CHECK(GetStackSizeForItem(linen, cfg, [](uint32_t lo, uint32_t hi) { return (hi == 99) ? 0 : lo; }) == 5);
        CHECK(GetStackSizeForItem(linen, cfg, [](uint32_t, uint32_t hi) { return (hi == 99) ? 0 : hi; }) == 20);
        // Roll 75 (>=75) falls back to single item.
        CHECK(GetStackSizeForItem(linen, cfg, [](uint32_t, uint32_t hi) { return (hi == 99) ? 75 : hi; }) == 1);

        // MaxStackSize cap clamps stack size.
        cfg.listingStackMaxStackSize[ai::kAhBotPriceClassTradeGoods] = 10;
        CHECK(GetStackSizeForItem(linen, cfg, [](uint32_t, uint32_t hi) { return (hi == 99) ? 0 : hi; }) == 10);

        // Projectile increment 1000 clamps to item max stack (200).
        AhBotItemSpec arrow{};
        arrow.itemClass = ai::kAhBotPriceClassProjectile;
        arrow.maxStackSize = 200;
        CHECK(GetStackSizeForItem(arrow, cfg, [](uint32_t, uint32_t hi) { return (hi == 99) ? 0 : hi; }) == 200);
    }

    // 3. CompleteItemValueOverride
    {
        AhBotPricingConfig cfg = MakeDefaultVanillaPricingConfig();
        cfg.completeItemValueOverrideEnabled = true;
        cfg.completeItemValueOverrideItems[2589] = 12345;

        AhBotItemSpec linen{};
        linen.itemId = 2589;
        linen.itemClass = ai::kAhBotPriceClassTradeGoods;
        linen.quality = ai::kAhBotPriceQualityNormal;
        linen.sellPrice = 13;

        uint64_t bid = 0, buyout = 0;
        CalculateItemValue(linen, cfg, [](uint32_t lo, uint32_t hi) { return (lo + hi) / 2; }, bid, buyout);
        CHECK(buyout == 12345);
        CHECK(bid == 12345);

        cfg.completeItemValueOverrideDoApplyBuyoutVariations = true;
        cfg.completeItemValueOverrideDoApplyBidVariations = true;
        CalculateItemValue(linen, cfg, [](uint32_t lo, uint32_t) { return lo; }, bid, buyout);
        // -15% on buyout, then -25% on bid
        CHECK(buyout == static_cast<uint64_t>(12345 * 0.85));
        CHECK(bid < buyout);
    }

    // 4. Midpoint vanilla calibration sanity checks
    {
        AhBotPricingConfig cfg = MakeDefaultVanillaPricingConfig();
        auto midRand = [](uint32_t lo, uint32_t hi) { return lo + (hi - lo) / 2; };

        auto checkMid = [&](AhBotItemSpec const& item, uint64_t minExpected, uint64_t maxExpected)
        {
            uint64_t bid = 0, buyout = 0;
            CalculateItemValue(item, cfg, midRand, bid, buyout);
            CHECK(buyout >= minExpected && buyout <= maxExpected);
            CHECK(bid >= 1 && bid <= buyout);
            CHECK(buyout >= item.sellPrice);
        };

        // Copper Ore: sell 5c, ilvl 10 -> ~18c
        checkMid({ 2770, ai::kAhBotPriceClassTradeGoods, 0, ai::kAhBotPriceQualityNormal, 10, 5, 10, 13, 0 }, 15, 22);
        // Linen Cloth: sell 13c, ilvl 5 -> ~44c
        checkMid({ 2589, ai::kAhBotPriceClassTradeGoods, 0, ai::kAhBotPriceQualityNormal, 5, 13, 20, 0, 0 }, 38, 52);
        // Runecloth: sell 400c, ilvl 50 -> ~2200c
        checkMid({ 14047, ai::kAhBotPriceClassTradeGoods, 0, ai::kAhBotPriceQualityNormal, 50, 400, 20, 0, 0 }, 1900, 2500);
        // Peacebloom: sell 10c, ilvl 5 -> ~82c
        checkMid({ 2447, ai::kAhBotPriceClassTradeGoods, 0, ai::kAhBotPriceQualityNormal, 5, 10, 20, 6, 0 }, 70, 95);
        // Major Healing Potion: sell 1000c, ilvl 55 -> ~9200c
        checkMid({ 13446, ai::kAhBotPriceClassConsumable, 0, ai::kAhBotPriceQualityNormal, 55, 1000, 10, 0, 0 }, 8000, 10500);
        // Flask of the Titans: sell 5000c, ilvl 60 -> ~22g-25g
        checkMid({ 13510, ai::kAhBotPriceClassConsumable, 0, ai::kAhBotPriceQualityNormal, 60, 5000, 10, 0, 0 }, 200000, 260000);
        // Strange Dust: sell 0c, min center override 60c -> ~239c
        checkMid({ 10940, ai::kAhBotPriceClassTradeGoods, 0, ai::kAhBotPriceQualityNormal, 10, 0, 20, 7, 0 }, 200, 280);
        // Large Brilliant Shard: sell 0c, min center override 4000c -> ~19000c
        checkMid({ 14344, ai::kAhBotPriceClassTradeGoods, 0, ai::kAhBotPriceQualityRare, 55, 0, 20, 7, 0 }, 16000, 22000);
        // Uncommon Armor ilvl 45: sell 6692c -> ~32800c
        checkMid({ 7332, ai::kAhBotPriceClassArmor, 4, ai::kAhBotPriceQualityUncommon, 45, 6692, 1, 0, 0 }, 29000, 37000);
        // Rare Weapon ilvl 50: sell 35042c -> ~174000c
        checkMid({ 13046, ai::kAhBotPriceClassWeapon, 4, ai::kAhBotPriceQualityRare, 50, 35042, 1, 0, 0 }, 155000, 195000);
    }

    // 5. BagFamily / LockID fallback for custom items & disabling advanced pricing
    {
        AhBotPricingConfig cfg = MakeDefaultVanillaPricingConfig();
        AhBotItemSpec customHerb{ 99001, ai::kAhBotPriceClassTradeGoods, 0, ai::kAhBotPriceQualityNormal, 30, 100, 20, ai::kAhBotBagFamilyHerbs, 0 };
        CHECK(ClassifyAdvancedGroup(customHerb, cfg) == AhBotAdvancedGroup::TradeGoodHerb);
        CHECK(GetAdvancedPricingMultiplier(customHerb, cfg) > 1.5f);

        cfg.advancedPricingEnabled[static_cast<size_t>(AhBotAdvancedGroup::TradeGoodHerb)] = false;
        CHECK(GetAdvancedPricingMultiplier(customHerb, cfg) == 1.0f);

        AhBotItemSpec customBox{ 99002, ai::kAhBotPriceClassMisc, 0, ai::kAhBotPriceQualityUncommon, 40, 150, 1, 0, 42 };
        CHECK(ClassifyAdvancedGroup(customBox, cfg) == AhBotAdvancedGroup::MiscJunk);
    }

    // 6. ItemLevel multiplier only applies when advancedPricingMultiplier == 1.0
    {
        AhBotPricingConfig cfg = MakeDefaultVanillaPricingConfig();
        cfg.buyoutVariationReducePercent = 0.0f;
        cfg.buyoutVariationAddPercent = 0.0f;
        cfg.bidVariationLowReducePercent = 0.0f;
        cfg.priceMultiplierCategoryQuality[ai::kAhBotPriceClassArmor][ai::kAhBotPriceQualityUncommon] = 1.0f;
        cfg.priceMultiplierQuality[ai::kAhBotPriceQualityUncommon] = 1.0f;
        cfg.priceMultiplierItemLevelCategory[ai::kAhBotPriceClassArmor] = 0.1f;

        AhBotItemSpec armor{ 99003, ai::kAhBotPriceClassArmor, 1, ai::kAhBotPriceQualityUncommon, 40, 1000, 1, 0, 0 };
        uint64_t bid = 0, buyout = 0;
        CalculateItemValue(armor, cfg, [](uint32_t lo, uint32_t) { return lo; }, bid, buyout);
        // 1000 * (40 * 0.1) = 4000
        CHECK(buyout == 4000);
    }

    // 7. Vendor floor protection, MaxBuyout cap, Listing stack multiply, BuyerFairValuePerUnit
    {
        AhBotPricingConfig cfg = MakeDefaultVanillaPricingConfig();
        cfg.buyoutVariationReducePercent = 0.5f;
        cfg.buyoutVariationAddPercent = 0.0f;
        cfg.priceMultiplierCategoryQuality[ai::kAhBotPriceClassArmor][ai::kAhBotPriceQualityNormal] = 0.2f;
        cfg.buyoutBelowVendorVariationAddPercentEnabled = true;
        cfg.buyoutBelowVendorVariationAddPercent = 0.25f;

        AhBotItemSpec vendorItem{ 99004, ai::kAhBotPriceClassArmor, 1, ai::kAhBotPriceQualityNormal, 10, 500, 1, 0, 0 };
        uint64_t bid = 0, buyout = 0;
        CalculateItemValue(vendorItem, cfg, [](uint32_t lo, uint32_t) { return lo; }, bid, buyout);
        CHECK(buyout >= 500);

        cfg.maxBuyoutPriceInCopper = 300;
        cfg.buyoutBelowVendorVariationAddPercentEnabled = false;
        CalculateItemValue(vendorItem, cfg, [](uint32_t lo, uint32_t) { return lo; }, bid, buyout);
        CHECK(buyout <= 300);

        // Listing price multiplies unit price by stack count
        cfg = MakeDefaultVanillaPricingConfig();
        cfg.buyoutVariationReducePercent = 0.0f;
        cfg.buyoutVariationAddPercent = 0.0f;
        cfg.bidVariationLowReducePercent = 0.0f;
        AhBotItemSpec linen{ 2589, ai::kAhBotPriceClassTradeGoods, 0, ai::kAhBotPriceQualityNormal, 5, 13, 20, 0, 0 };
        AhBotPricePair p1 = CalculateListingPrice(linen, 1, cfg, [](uint32_t lo, uint32_t) { return lo; });
        AhBotPricePair p10 = CalculateListingPrice(linen, 10, cfg, [](uint32_t lo, uint32_t) { return lo; });
        CHECK(p10.buyout == p1.buyout * 10);
        CHECK(p10.bid == p1.bid * 10);

        cfg.buyerAcceptablePriceModifier = 1.5f;
        uint64_t fair = CalculateBuyerFairValuePerUnit(linen, cfg, [](uint32_t lo, uint32_t) { return lo; });
        CHECK(fair == static_cast<uint64_t>(p1.buyout * 1.5));
    }

    if (failures)
    {
        std::cerr << "ahbot-pricing policy: " << failures << " FAILURES\n";
        return 1;
    }
    std::cout << "ahbot-pricing policy: OK\n";
    return 0;
}
