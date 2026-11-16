#pragma once

/*
 * Copyright (C) 2008-2010 Trinity <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2023+ Nathan Handley <https://github.com/NathanHandley>
 *
 * Ported from mod-ah-bot-plus AuctionHouseBot.cpp@f6858329
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

// Pure pricing and stack-sizing policy for the AH bot (issue #1639).
// Mirrors mod-ah-bot-plus CalculateItemValue and GetStackSizeForItem with
// vanilla-calibrated defaults and item-id based advanced pricing rules
// (since vanilla 1.12 item_template uses SubClass 0 across consumables and
// trade goods). No core includes so standalone g++ tests compile directly.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace ai
{
inline constexpr uint32_t kAhBotMaxItemClass = 16;
inline constexpr uint32_t kAhBotMaxItemQuality = 7;

enum AhBotItemClass : uint32_t
{
    kAhBotClassConsumable = 0,
    kAhBotClassContainer = 1,
    kAhBotClassWeapon = 2,
    kAhBotClassGem = 3,
    kAhBotClassArmor = 4,
    kAhBotClassReagent = 5,
    kAhBotClassProjectile = 6,
    kAhBotClassTradeGoods = 7,
    kAhBotClassGeneric = 8,
    kAhBotClassRecipe = 9,
    kAhBotClassMoney = 10,
    kAhBotClassQuiver = 11,
    kAhBotClassQuest = 12,
    kAhBotClassKey = 13,
    kAhBotClassPermanent = 14,
    kAhBotClassMisc = 15,
};

enum AhBotItemQuality : uint32_t
{
    kAhBotQualityPoor = 0,
    kAhBotQualityNormal = 1,
    kAhBotQualityUncommon = 2,
    kAhBotQualityRare = 3,
    kAhBotQualityEpic = 4,
    kAhBotQualityLegendary = 5,
    kAhBotQualityArtifact = 6,
};

enum class AhBotAdvancedGroup : uint8_t
{
    None = 0,
    ConsumablePotion,
    ConsumableElixir,
    ConsumableFlask,
    TradeGoodCloth,
    TradeGoodHerb,
    TradeGoodMetalStone,
    TradeGoodLeather,
    TradeGoodEnchanting,
    TradeGoodElemental,
    TradeGoodMeat,
    MiscJunk,
    MiscMount,
    MiscPet,
    Count
};

// BagFamily constants matching core ItemPrototype.h.
inline constexpr uint32_t kAhBotBagFamilyHerbs = 6;
inline constexpr uint32_t kAhBotBagFamilyEnchanting = 7;
inline constexpr uint32_t kAhBotBagFamilyMeat = 10;
inline constexpr uint32_t kAhBotBagFamilyLeather = 12;
inline constexpr uint32_t kAhBotBagFamilyMining = 13;

// Misc subclasses matching core ItemPrototype.h.
inline constexpr uint32_t kAhBotSubClassMiscPet = 2;
inline constexpr uint32_t kAhBotSubClassMiscMount = 5;

struct AhBotItemSpec
{
    uint32_t itemId = 0;
    uint32_t itemClass = 0;
    uint32_t itemSubClass = 0;
    uint32_t quality = 0;
    uint32_t itemLevel = 0;
    uint32_t sellPrice = 0;
    uint32_t maxStackSize = 1;
    uint32_t bagFamily = 0;
    uint32_t lockId = 0;
};

struct AhBotPricePair
{
    uint32_t bid = 0;
    uint32_t buyout = 0;
};

struct AhBotPricingConfig
{
    bool completeItemValueOverrideEnabled = false;
    std::unordered_map<uint32_t, uint32_t> completeItemValueOverrideItems;
    bool completeItemValueOverrideDoApplyBidVariations = false;
    bool completeItemValueOverrideDoApplyBuyoutVariations = false;

    uint32_t maxBuyoutPriceInCopper = 1000000000;
    float buyoutVariationReducePercent = 0.15f;
    float buyoutVariationAddPercent = 0.25f;
    float bidVariationHighReducePercent = 0.0f;
    float bidVariationLowReducePercent = 0.25f;
    bool buyoutBelowVendorVariationAddPercentEnabled = true;
    float buyoutBelowVendorVariationAddPercent = 0.25f;
    float buyerAcceptablePriceModifier = 1.0f;

    bool useItemSellPriceIfHigherThanPriceMinimumCenterBase = true;
    std::array<uint32_t, kAhBotMaxItemClass> priceMinimumCenterBase{};
    std::unordered_map<uint32_t, uint32_t> priceMinimumCenterBaseOverrides;

    std::array<bool, static_cast<size_t>(AhBotAdvancedGroup::Count)> advancedPricingEnabled{};
    std::unordered_map<uint32_t, AhBotAdvancedGroup> advancedPricingItemGroups;

    std::array<float, kAhBotMaxItemClass> priceMultiplierCategory{};
    std::array<float, kAhBotMaxItemQuality> priceMultiplierQuality{};
    std::array<float, kAhBotMaxItemClass> priceMultiplierItemLevelCategory{};
    std::array<std::array<float, kAhBotMaxItemQuality>, kAhBotMaxItemClass> priceMultiplierCategoryQuality{};
    std::array<float, kAhBotMaxItemQuality> priceMultiplierCategoryMountQuality{};
    std::array<float, kAhBotMaxItemQuality> priceMultiplierCategoryPetQuality{};

    std::array<uint32_t, kAhBotMaxItemClass> listingStackRandomRatio{};
    std::array<uint32_t, kAhBotMaxItemClass> listingStackRandomStackIncrement{};
    std::array<uint32_t, kAhBotMaxItemClass> listingStackMaxStackSize{};
};

// Parse comma-separated "itemID:value" pairs (first occurrence wins, matching std::unordered_map::insert).
inline std::unordered_map<uint32_t, uint32_t> ParseItemValuePairs(std::string const& spec)
{
    std::unordered_map<uint32_t, uint32_t> out;
    std::stringstream stream(spec);
    std::string token;
    while (std::getline(stream, token, ','))
    {
        std::stringstream pairStream(token);
        std::string block;
        pairStream >> block;
        size_t colon = block.find(':');
        if (colon == std::string::npos)
            continue;
        std::string idStr = block.substr(0, colon);
        std::string valStr = block.substr(colon + 1);
        if (idStr.empty() || valStr.empty())
            continue;

        auto parsePositiveU32 = [](std::string const& s) -> uint32_t
        {
            uint64_t acc = 0;
            for (char c : s)
            {
                if (c < '0' || c > '9')
                    return 0;
                acc = acc * 10 + static_cast<uint64_t>(c - '0');
                if (acc > std::numeric_limits<uint32_t>::max())
                    return std::numeric_limits<uint32_t>::max();
            }
            return static_cast<uint32_t>(acc);
        };

        uint32_t itemId = parsePositiveU32(idStr);
        uint32_t val = parsePositiveU32(valStr);
        if (itemId > 0 && val > 0)
            out.insert({ itemId, val });
    }
    return out;
}

inline constexpr char const* kAhBotDefaultMinCenterOverrides =
    "10940:60,11083:110,11137:220,11176:330,16204:550,"
    "10938:50,10939:150,10998:200,11082:350,11134:400,11135:800,11174:1000,11175:2500,16202:1500,16203:4000,"
    "10978:250,11084:400,11138:500,11139:800,11177:1000,11178:1500,14343:1500,14344:4000,20725:15000";

// Vanilla 1.12 / Turtle item-ID mapping for advanced pricing groups because
// vanilla item_template assigns SubClass 0 to nearly all consumables and trade goods.
inline std::unordered_map<uint32_t, AhBotAdvancedGroup> DefaultVanillaAdvancedGroupMap()
{
    std::unordered_map<uint32_t, AhBotAdvancedGroup> m;
    auto addList = [&](AhBotAdvancedGroup group, std::initializer_list<uint32_t> ids)
    {
        for (uint32_t id : ids)
            m[id] = group;
    };

    addList(AhBotAdvancedGroup::ConsumablePotion, {
        118, 858, 929, 1450, 1710, 2455, 2456, 2459, 3382, 3384, 3385, 3387, 3388,
        3823, 3827, 3928, 4596, 4623, 5631, 5632, 5633, 5634, 6048, 6049, 6050, 6051,
        6052, 6149, 6372, 9030, 9036, 9144, 9172, 12190, 13442, 13443, 13444, 13446,
        13455, 13456, 13457, 13458, 13459, 13460, 13461, 13462, 18253, 20002, 20004,
        20007, 20008, 41961, 61181, 61225
    });

    addList(AhBotAdvancedGroup::ConsumableElixir, {
        2454, 2457, 2458, 3383, 3386, 3389, 3390, 3391, 3825, 3826, 3828, 5996, 5997,
        6373, 6662, 8827, 8949, 8951, 9088, 9154, 9155, 9179, 9187, 9197, 9206, 9224,
        9233, 9264, 10592, 12820, 13445, 13447, 13452, 13453, 13454, 17708, 18294,
        21546, 42185, 50237, 55046, 55048, 56113, 61224
    });

    addList(AhBotAdvancedGroup::ConsumableFlask, {
        13506, 13510, 13511, 13512, 13513
    });

    addList(AhBotAdvancedGroup::TradeGoodCloth, {
        2589, 2592, 2996, 2997, 4305, 4306, 4338, 4339, 14047, 14048, 14256, 14342
    });

    addList(AhBotAdvancedGroup::TradeGoodHerb, {
        765, 785, 2447, 2449, 2450, 2452, 2453, 3355, 3356, 3357, 3358, 3369,
        3818, 3819, 3820, 3821, 4625, 5173, 8153, 8831, 8836, 8838, 8839, 8845,
        8846, 11040, 13463, 13464, 13465, 13466, 13467, 13468, 19726
    });

    addList(AhBotAdvancedGroup::TradeGoodMetalStone, {
        774, 818, 1206, 1210, 1529, 1705, 2770, 2771, 2772, 2775, 2776, 2835, 2836,
        2838, 2840, 2841, 2842, 3340, 3575, 3576, 3577, 3858, 3859, 3860, 3861, 3864,
        5498, 5500, 5833, 6037, 7909, 7910, 7911, 7912, 7971, 10620, 11099, 11370,
        11371, 11382, 12359, 12360, 12361, 12363, 12364, 12365, 12655, 12799, 12800,
        13926, 17771, 18562, 19774, 22202, 22203
    });

    addList(AhBotAdvancedGroup::TradeGoodLeather, {
        783, 2318, 2319, 2934, 4231, 4232, 4233, 4234, 4235, 4236, 4304, 4461, 5082,
        5784, 5785, 7286, 7287, 7428, 8154, 8165, 8167, 8169, 8170, 8171, 8172, 8368,
        12810, 15407, 15408, 15409, 15410, 15412, 15414, 15415, 15416, 15417, 15419,
        15422, 15423, 17012, 19767, 19768, 42215, 61229
    });

    addList(AhBotAdvancedGroup::TradeGoodEnchanting, {
        10938, 10939, 10940, 10978, 10998, 11082, 11083, 11084, 11134, 11135, 11137,
        11138, 11139, 11174, 11175, 11176, 11177, 11178, 14343, 14344, 16202, 16203,
        16204, 20725
    });

    addList(AhBotAdvancedGroup::TradeGoodElemental, {
        7067, 7068, 7069, 7070, 7075, 7076, 7077, 7078, 7079, 7080, 7081, 7082,
        7972, 12803, 12808
    });

    addList(AhBotAdvancedGroup::TradeGoodMeat, {
        723, 729, 731, 769, 1080, 1081, 2251, 2672, 2673, 2674, 2675, 2924, 3173,
        3404, 3667, 3685, 3712, 3730, 3731, 4655, 5465, 5468, 5469, 5471, 5503, 5504,
        6889, 7974, 12037, 12184, 12202, 12203, 12204, 12205, 12206, 12207, 12208,
        12223, 20424, 21024
    });

    addList(AhBotAdvancedGroup::MiscJunk, {
        4632, 4633, 4634, 4636, 4637, 4638, 5758, 5759, 5760, 6354, 6355,
        13875, 13918, 16882, 16883, 16884, 16885
    });

    return m;
}

inline AhBotPricingConfig MakeDefaultVanillaPricingConfig()
{
    AhBotPricingConfig cfg;
    cfg.advancedPricingEnabled.fill(true);
    cfg.advancedPricingItemGroups = DefaultVanillaAdvancedGroupMap();
    cfg.priceMinimumCenterBaseOverrides = ParseItemValuePairs(kAhBotDefaultMinCenterOverrides);

    cfg.priceMinimumCenterBase.fill(20);
    cfg.priceMinimumCenterBase[kAhBotClassConsumable] = 10;
    cfg.priceMinimumCenterBase[kAhBotClassContainer] = 50;
    cfg.priceMinimumCenterBase[kAhBotClassWeapon] = 25;
    cfg.priceMinimumCenterBase[kAhBotClassArmor] = 25;
    cfg.priceMinimumCenterBase[kAhBotClassReagent] = 20;
    cfg.priceMinimumCenterBase[kAhBotClassProjectile] = 1;
    cfg.priceMinimumCenterBase[kAhBotClassTradeGoods] = 5;
    cfg.priceMinimumCenterBase[kAhBotClassGeneric] = 20;
    cfg.priceMinimumCenterBase[kAhBotClassRecipe] = 20;
    cfg.priceMinimumCenterBase[kAhBotClassQuiver] = 25;
    cfg.priceMinimumCenterBase[kAhBotClassQuest] = 20;
    cfg.priceMinimumCenterBase[kAhBotClassKey] = 25;
    cfg.priceMinimumCenterBase[kAhBotClassMisc] = 20;

    cfg.priceMultiplierCategory.fill(1.0f);
    cfg.priceMultiplierItemLevelCategory.fill(0.0f);

    cfg.priceMultiplierQuality[kAhBotQualityPoor] = 1.0f;
    cfg.priceMultiplierQuality[kAhBotQualityNormal] = 1.0f;
    cfg.priceMultiplierQuality[kAhBotQualityUncommon] = 1.8f;
    cfg.priceMultiplierQuality[kAhBotQualityRare] = 1.9f;
    cfg.priceMultiplierQuality[kAhBotQualityEpic] = 2.1f;
    cfg.priceMultiplierQuality[kAhBotQualityLegendary] = 3.0f;
    cfg.priceMultiplierQuality[kAhBotQualityArtifact] = 3.0f;

    for (auto& row : cfg.priceMultiplierCategoryQuality)
        row.fill(1.0f);

    cfg.priceMultiplierCategoryQuality[kAhBotClassConsumable][kAhBotQualityNormal] = 4.3f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassConsumable][kAhBotQualityUncommon] = 1.8f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassConsumable][kAhBotQualityRare] = 1.7f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassConsumable][kAhBotQualityEpic] = 2.0f;

    cfg.priceMultiplierCategoryQuality[kAhBotClassContainer][kAhBotQualityNormal] = 3.5f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassContainer][kAhBotQualityUncommon] = 3.1f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassContainer][kAhBotQualityRare] = 2.5f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassContainer][kAhBotQualityEpic] = 2.5f;

    cfg.priceMultiplierCategoryQuality[kAhBotClassWeapon][kAhBotQualityNormal] = 1.5f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassWeapon][kAhBotQualityUncommon] = 2.25f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassWeapon][kAhBotQualityRare] = 2.5f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassWeapon][kAhBotQualityEpic] = 2.3f;

    cfg.priceMultiplierCategoryQuality[kAhBotClassArmor][kAhBotQualityNormal] = 1.5f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassArmor][kAhBotQualityUncommon] = 2.6f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassArmor][kAhBotQualityRare] = 2.5f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassArmor][kAhBotQualityEpic] = 2.7f;

    cfg.priceMultiplierCategoryQuality[kAhBotClassReagent][kAhBotQualityNormal] = 5.0f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassReagent][kAhBotQualityUncommon] = 2.5f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassReagent][kAhBotQualityRare] = 2.5f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassReagent][kAhBotQualityEpic] = 2.5f;

    cfg.priceMultiplierCategoryQuality[kAhBotClassProjectile][kAhBotQualityNormal] = 0.9f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassProjectile][kAhBotQualityUncommon] = 0.8f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassProjectile][kAhBotQualityRare] = 2.5f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassProjectile][kAhBotQualityEpic] = 2.8f;

    cfg.priceMultiplierCategoryQuality[kAhBotClassTradeGoods][kAhBotQualityNormal] = 3.8f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassTradeGoods][kAhBotQualityUncommon] = 2.1f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassTradeGoods][kAhBotQualityRare] = 2.4f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassTradeGoods][kAhBotQualityEpic] = 2.5f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassTradeGoods][kAhBotQualityLegendary] = 1.4f;

    cfg.priceMultiplierCategoryQuality[kAhBotClassRecipe][kAhBotQualityNormal] = 4.0f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassRecipe][kAhBotQualityUncommon] = 2.75f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassRecipe][kAhBotQualityRare] = 2.7f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassRecipe][kAhBotQualityEpic] = 2.5f;

    cfg.priceMultiplierCategoryQuality[kAhBotClassQuest][kAhBotQualityRare] = 7.0f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassQuest][kAhBotQualityEpic] = 8.0f;

    cfg.priceMultiplierCategoryQuality[kAhBotClassMisc][kAhBotQualityNormal] = 3.2f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassMisc][kAhBotQualityUncommon] = 2.55f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassMisc][kAhBotQualityRare] = 1.7f;
    cfg.priceMultiplierCategoryQuality[kAhBotClassMisc][kAhBotQualityEpic] = 2.0f;

    cfg.priceMultiplierCategoryMountQuality.fill(1.0f);
    cfg.priceMultiplierCategoryMountQuality[kAhBotQualityRare] = 3000.0f;
    cfg.priceMultiplierCategoryMountQuality[kAhBotQualityEpic] = 5750.0f;

    cfg.priceMultiplierCategoryPetQuality.fill(1.0f);

    cfg.listingStackRandomRatio.fill(0);
    cfg.listingStackRandomRatio[kAhBotClassConsumable] = 50;
    cfg.listingStackRandomRatio[kAhBotClassReagent] = 50;
    cfg.listingStackRandomRatio[kAhBotClassProjectile] = 100;
    cfg.listingStackRandomRatio[kAhBotClassTradeGoods] = 75;
    cfg.listingStackRandomRatio[kAhBotClassGeneric] = 100;
    cfg.listingStackRandomRatio[kAhBotClassQuest] = 10;
    cfg.listingStackRandomRatio[kAhBotClassKey] = 10;
    cfg.listingStackRandomRatio[kAhBotClassMisc] = 100;

    cfg.listingStackRandomStackIncrement.fill(1);
    cfg.listingStackRandomStackIncrement[kAhBotClassConsumable] = 5;
    cfg.listingStackRandomStackIncrement[kAhBotClassProjectile] = 1000;
    cfg.listingStackRandomStackIncrement[kAhBotClassTradeGoods] = 5;

    cfg.listingStackMaxStackSize.fill(0);
    return cfg;
}

inline AhBotAdvancedGroup ClassifyAdvancedGroup(AhBotItemSpec const& item, AhBotPricingConfig const& cfg)
{
    auto it = cfg.advancedPricingItemGroups.find(item.itemId);
    if (it != cfg.advancedPricingItemGroups.end())
        return it->second;

    if (item.itemClass == kAhBotClassMisc)
    {
        if (item.itemSubClass == kAhBotSubClassMiscPet)
            return AhBotAdvancedGroup::MiscPet;
        if (item.itemSubClass == kAhBotSubClassMiscMount)
            return AhBotAdvancedGroup::MiscMount;
        if (item.lockId > 0)
            return AhBotAdvancedGroup::MiscJunk;
    }
    else if (item.itemClass == kAhBotClassTradeGoods)
    {
        switch (item.bagFamily)
        {
            case kAhBotBagFamilyHerbs:       return AhBotAdvancedGroup::TradeGoodHerb;
            case kAhBotBagFamilyEnchanting:  return AhBotAdvancedGroup::TradeGoodEnchanting;
            case kAhBotBagFamilyMeat:        return AhBotAdvancedGroup::TradeGoodMeat;
            case kAhBotBagFamilyLeather:     return AhBotAdvancedGroup::TradeGoodLeather;
            case kAhBotBagFamilyMining:      return AhBotAdvancedGroup::TradeGoodMetalStone;
            default:                         break;
        }
    }
    return AhBotAdvancedGroup::None;
}

inline float GetAdvancedPricingMultiplier(AhBotItemSpec const& item, AhBotPricingConfig const& cfg)
{
    AhBotAdvancedGroup group = ClassifyAdvancedGroup(item, cfg);
    if (group == AhBotAdvancedGroup::None)
        return 1.0f;
    size_t idx = static_cast<size_t>(group);
    if (idx >= cfg.advancedPricingEnabled.size() || !cfg.advancedPricingEnabled[idx])
        return 1.0f;

    double ilvl = static_cast<double>(std::max<uint32_t>(item.itemLevel, 1));
    switch (group)
    {
        case AhBotAdvancedGroup::ConsumablePotion:
            return 2.05f;
        case AhBotAdvancedGroup::ConsumableElixir:
            return 2.05f;
        case AhBotAdvancedGroup::ConsumableFlask:
        {
            if (item.sellPrice == 0)
                return 10.5f;
            double target = 220000.0 + 30000.0 * (std::log(std::max<double>(item.sellPrice, 1250.0)) - std::log(1250.0)) / (std::log(10000.0) - std::log(1250.0));
            double denom = std::max(1.0, static_cast<double>(item.sellPrice) * 4.3 * 1.05);
            return static_cast<float>(std::max(1.0, target / denom));
        }
        case AhBotAdvancedGroup::TradeGoodCloth:
            return static_cast<float>(std::max(0.5, 0.78 + 0.012 * ilvl));
        case AhBotAdvancedGroup::TradeGoodHerb:
            return 2.05f;
        case AhBotAdvancedGroup::TradeGoodMetalStone:
            return static_cast<float>(std::max(0.5, 0.78 + 0.011 * ilvl));
        case AhBotAdvancedGroup::TradeGoodLeather:
            return static_cast<float>(std::max(0.5, 1.0 + 0.025 * ilvl));
        case AhBotAdvancedGroup::TradeGoodEnchanting:
            return 1.0f;
        case AhBotAdvancedGroup::TradeGoodElemental:
            return (item.quality >= kAhBotQualityUncommon) ? 7.0f : 1.0f;
        case AhBotAdvancedGroup::TradeGoodMeat:
            return 0.85f;
        case AhBotAdvancedGroup::MiscJunk:
            return static_cast<float>(std::max(0.5, 0.65 + 0.0095 * ilvl));
        case AhBotAdvancedGroup::MiscMount:
            return item.quality < kAhBotMaxItemQuality ? cfg.priceMultiplierCategoryMountQuality[item.quality] : 1.0f;
        case AhBotAdvancedGroup::MiscPet:
            return item.quality < kAhBotMaxItemQuality ? cfg.priceMultiplierCategoryPetQuality[item.quality] : 1.0f;
        default:
            return 1.0f;
    }
}

template <typename RandRangeFn>
inline uint32_t GetStackSizeForItem(AhBotItemSpec const& item, AhBotPricingConfig const& cfg, RandRangeFn&& randRange)
{
    uint32_t stackRatio = 0;
    uint32_t stackIncrement = 1;
    uint32_t configStackSizeMax = 0;
    if (item.itemClass < kAhBotMaxItemClass)
    {
        stackRatio = cfg.listingStackRandomRatio[item.itemClass];
        stackIncrement = cfg.listingStackRandomStackIncrement[item.itemClass];
        configStackSizeMax = cfg.listingStackMaxStackSize[item.itemClass];
    }
    stackIncrement = std::max<uint32_t>(stackIncrement, 1);

    if (stackRatio > randRange(0, 99))
    {
        uint32_t maxPossibleStackSize = std::max<uint32_t>(1, item.maxStackSize);
        if (configStackSizeMax != 0)
            maxPossibleStackSize = std::min(configStackSizeMax, maxPossibleStackSize);
        uint32_t numOfPossibleStackIncrements = static_cast<uint32_t>(
            std::ceil(static_cast<double>(maxPossibleStackSize) / static_cast<double>(stackIncrement)));
        numOfPossibleStackIncrements = std::max<uint32_t>(numOfPossibleStackIncrements, 1);
        uint32_t numOfStacks = randRange(1, numOfPossibleStackIncrements);
        uint32_t randomStackSize = numOfStacks * stackIncrement;
        return std::min(randomStackSize, maxPossibleStackSize);
    }
    return 1;
}

template <typename RandRangeFn>
inline void CalculateItemValue(AhBotItemSpec const& item, AhBotPricingConfig const& cfg, RandRangeFn&& randRange,
    uint64_t& outBidPrice, uint64_t& outBuyoutPrice)
{
    auto safeRand64 = [&](uint64_t lo, uint64_t hi) -> uint64_t
    {
        if (lo >= hi)
            return lo;
        uint64_t span = hi - lo;
        if (span <= std::numeric_limits<uint32_t>::max())
            return lo + static_cast<uint64_t>(randRange(0, static_cast<uint32_t>(span)));
        uint64_t hi32 = std::numeric_limits<uint32_t>::max();
        return lo + static_cast<uint64_t>(randRange(0, static_cast<uint32_t>(hi32)));
    };

    auto variedRange = [&](uint64_t center, float reducePct, float addPct) -> uint64_t
    {
        double lowFactor = std::max(0.0, 1.0 - static_cast<double>(std::max(0.0f, reducePct)));
        double highFactor = std::max(lowFactor, 1.0 + static_cast<double>(std::max(0.0f, addPct)));
        uint64_t lo = static_cast<uint64_t>(static_cast<double>(center) * lowFactor);
        uint64_t hi = static_cast<uint64_t>(static_cast<double>(center) * highFactor);
        return safeRand64(lo, hi);
    };

    auto applyBidVariation = [&](uint64_t buyout) -> uint64_t
    {
        double lowFactor = std::max(0.0, 1.0 - static_cast<double>(std::max(0.0f, cfg.bidVariationLowReducePercent)));
        double highFactor = std::max(0.0, 1.0 - static_cast<double>(std::max(0.0f, cfg.bidVariationHighReducePercent)));
        if (lowFactor > highFactor)
            std::swap(lowFactor, highFactor);
        uint64_t lo = static_cast<uint64_t>(static_cast<double>(buyout) * lowFactor);
        uint64_t hi = static_cast<uint64_t>(static_cast<double>(buyout) * highFactor);
        return safeRand64(lo, hi);
    };

    if (cfg.completeItemValueOverrideEnabled)
    {
        auto it = cfg.completeItemValueOverrideItems.find(item.itemId);
        if (it != cfg.completeItemValueOverrideItems.end())
        {
            outBuyoutPrice = it->second;
            if (cfg.completeItemValueOverrideDoApplyBuyoutVariations)
                outBuyoutPrice = variedRange(outBuyoutPrice, cfg.buyoutVariationReducePercent, cfg.buyoutVariationAddPercent);

            if (cfg.completeItemValueOverrideDoApplyBidVariations)
                outBidPrice = applyBidVariation(outBuyoutPrice);
            else
                outBidPrice = outBuyoutPrice;

            if (outBuyoutPrice == 0)
                outBuyoutPrice = 1;
            if (outBidPrice == 0)
                outBidPrice = 1;
            if (outBidPrice > outBuyoutPrice)
                outBidPrice = outBuyoutPrice;
            return;
        }
    }

    if (cfg.useItemSellPriceIfHigherThanPriceMinimumCenterBase)
        outBuyoutPrice = item.sellPrice;
    else
        outBuyoutPrice = 1;

    float classPriceMultiplier = (item.itemClass < kAhBotMaxItemClass) ? cfg.priceMultiplierCategory[item.itemClass] : 1.0f;
    float qualityPriceMultiplier = (item.quality < kAhBotMaxItemQuality) ? cfg.priceMultiplierQuality[item.quality] : 1.0f;
    float classQualityPriceMultiplier = (item.itemClass < kAhBotMaxItemClass && item.quality < kAhBotMaxItemQuality)
        ? cfg.priceMultiplierCategoryQuality[item.itemClass][item.quality]
        : 1.0f;
    float advancedPricingMultiplier = GetAdvancedPricingMultiplier(item, cfg);

    uint64_t priceMinimumCenterBase = 20;
    auto minIt = cfg.priceMinimumCenterBaseOverrides.find(item.itemId);
    if (minIt != cfg.priceMinimumCenterBaseOverrides.end())
        priceMinimumCenterBase = minIt->second;
    else if (item.itemClass < kAhBotMaxItemClass)
        priceMinimumCenterBase = cfg.priceMinimumCenterBase[item.itemClass];

    if (outBuyoutPrice < priceMinimumCenterBase)
        outBuyoutPrice = variedRange(priceMinimumCenterBase, cfg.buyoutVariationReducePercent, cfg.buyoutVariationAddPercent);
    else
        outBuyoutPrice = variedRange(outBuyoutPrice, cfg.buyoutVariationReducePercent, cfg.buyoutVariationAddPercent);

    if (classPriceMultiplier <= 0.0f)
        classPriceMultiplier = 1.0f;
    if (qualityPriceMultiplier <= 0.0f)
        qualityPriceMultiplier = 1.0f;
    if (classQualityPriceMultiplier <= 0.0f)
        classQualityPriceMultiplier = 1.0f;
    if (advancedPricingMultiplier <= 0.0f)
        advancedPricingMultiplier = 1.0f;

    float itemLevelPriceMultiplier = (item.itemClass < kAhBotMaxItemClass) ? cfg.priceMultiplierItemLevelCategory[item.itemClass] : 0.0f;

    double scaled = static_cast<double>(outBuyoutPrice);
    scaled *= static_cast<double>(qualityPriceMultiplier);
    scaled *= static_cast<double>(classPriceMultiplier);
    scaled *= static_cast<double>(classQualityPriceMultiplier);
    scaled *= static_cast<double>(advancedPricingMultiplier);

    if (itemLevelPriceMultiplier > 0.0f && item.itemLevel > 0 && advancedPricingMultiplier == 1.0f)
        scaled *= static_cast<double>(item.itemLevel) * static_cast<double>(itemLevelPriceMultiplier);

    outBuyoutPrice = static_cast<uint64_t>(std::max(0.0, scaled));

    if (cfg.maxBuyoutPriceInCopper > 0 && outBuyoutPrice > cfg.maxBuyoutPriceInCopper)
        outBuyoutPrice = cfg.maxBuyoutPriceInCopper;

    if (cfg.buyoutBelowVendorVariationAddPercentEnabled && outBuyoutPrice < item.sellPrice)
    {
        double vendorHigh = static_cast<double>(item.sellPrice) * (1.0 + static_cast<double>(std::max(0.0f, cfg.buyoutBelowVendorVariationAddPercent)));
        outBuyoutPrice = safeRand64(item.sellPrice, static_cast<uint64_t>(vendorHigh));
    }

    outBidPrice = applyBidVariation(outBuyoutPrice);

    if (outBuyoutPrice == 0)
        outBuyoutPrice = 1;
    if (outBidPrice == 0)
        outBidPrice = 1;
    if (outBidPrice > outBuyoutPrice)
        outBidPrice = outBuyoutPrice;
}

template <typename RandRangeFn>
inline AhBotPricePair CalculateListingPrice(AhBotItemSpec const& item, uint32_t count, AhBotPricingConfig const& cfg, RandRangeFn&& randRange)
{
    uint32_t safeCount = std::max<uint32_t>(1, count);
    uint64_t unitBid = 0;
    uint64_t unitBuyout = 0;
    CalculateItemValue(item, cfg, std::forward<RandRangeFn>(randRange), unitBid, unitBuyout);

    uint64_t totalBuyout = unitBuyout * static_cast<uint64_t>(safeCount);
    uint64_t totalBid = unitBid * static_cast<uint64_t>(safeCount);
    uint64_t maxU32 = std::numeric_limits<uint32_t>::max();
    totalBuyout = std::min(totalBuyout, maxU32);
    totalBid = std::min(totalBid, totalBuyout);

    AhBotPricePair out;
    out.buyout = static_cast<uint32_t>(std::max<uint64_t>(1, totalBuyout));
    out.bid = static_cast<uint32_t>(std::max<uint64_t>(1, totalBid));
    return out;
}

template <typename RandRangeFn>
inline uint64_t CalculateBuyerFairValuePerUnit(AhBotItemSpec const& item, AhBotPricingConfig const& cfg, RandRangeFn&& randRange)
{
    uint64_t discardBid = 0;
    uint64_t willingPerUnit = 0;
    CalculateItemValue(item, cfg, std::forward<RandRangeFn>(randRange), discardBid, willingPerUnit);
    double mod = static_cast<double>(std::max(0.0f, cfg.buyerAcceptablePriceModifier));
    return static_cast<uint64_t>(static_cast<double>(willingPerUnit) * mod);
}

} // namespace ai
