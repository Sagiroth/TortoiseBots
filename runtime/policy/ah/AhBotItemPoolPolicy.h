/*
 * Copyright (C) 2008-2010 Trinity <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2023+ Nathan Handley <https://github.com/NathanHandley>
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
 *
 * Ported from mod-ah-bot-plus AuctionHouseBot.cpp@f6858329.
 */

#pragma once

// Pure item pool filtering and weighted category x quality selection policy
// for the AH bot. Std-only (no core includes) so standalone g++ tests can
// exercise the exact filtering and proportion rules directly.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ai
{

inline constexpr uint32_t kAhBotMaxItemClass = 16;
inline constexpr uint32_t kAhBotMaxItemQuality = 7;

// Vanilla item classes and qualities matching core ItemPrototype.h / SharedDefines.h.
inline constexpr uint32_t kAhBotClassConsumable = 0;
inline constexpr uint32_t kAhBotClassContainer = 1;
inline constexpr uint32_t kAhBotClassWeapon = 2;
inline constexpr uint32_t kAhBotClassGem = 3;
inline constexpr uint32_t kAhBotClassArmor = 4;
inline constexpr uint32_t kAhBotClassReagent = 5;
inline constexpr uint32_t kAhBotClassProjectile = 6;
inline constexpr uint32_t kAhBotClassTradeGoods = 7;
inline constexpr uint32_t kAhBotClassGeneric = 8;
inline constexpr uint32_t kAhBotClassRecipe = 9;
inline constexpr uint32_t kAhBotClassMoney = 10;
inline constexpr uint32_t kAhBotClassQuiver = 11;
inline constexpr uint32_t kAhBotClassQuest = 12;
inline constexpr uint32_t kAhBotClassKey = 13;
inline constexpr uint32_t kAhBotClassPermanent = 14;
inline constexpr uint32_t kAhBotClassMisc = 15;

inline constexpr uint32_t kAhBotQualityPoor = 0;
inline constexpr uint32_t kAhBotQualityNormal = 1;
inline constexpr uint32_t kAhBotQualityUncommon = 2;
inline constexpr uint32_t kAhBotQualityRare = 3;
inline constexpr uint32_t kAhBotQualityEpic = 4;
inline constexpr uint32_t kAhBotQualityLegendary = 5;
inline constexpr uint32_t kAhBotQualityArtifact = 6;

inline constexpr uint32_t kAhBotBindWhenPickedUp = 1;
inline constexpr uint32_t kAhBotBindQuestItem = 4;
inline constexpr uint32_t kAhBotBindQuestItem1 = 5;
inline constexpr uint32_t kAhBotBindAccount = 6;

inline constexpr uint32_t kAhBotItemFlagConjured = 0x00000002;
inline constexpr uint32_t kAhBotItemExtraNotObtainable = 0x04;

inline constexpr uint32_t kAhBotSubclassConsumableEnhancement = 6;
inline constexpr uint32_t kAhBotSubclassTradeGoodsEnchanting = 12;
inline constexpr uint32_t kAhBotSubclassRecipeBook = 0;

// Returns the vanilla AH bot category name, or nullptr for classes excluded from
// the AH bot (Gem, Money, Permanent, or out-of-range).
inline char const* AhBotCategoryName(uint32_t itemClass)
{
    switch (itemClass)
    {
        case kAhBotClassConsumable: return "Consumable";
        case kAhBotClassContainer:  return "Container";
        case kAhBotClassWeapon:     return "Weapon";
        case kAhBotClassArmor:      return "Armor";
        case kAhBotClassReagent:    return "Reagent";
        case kAhBotClassProjectile: return "Projectile";
        case kAhBotClassTradeGoods: return "TradeGood";
        case kAhBotClassGeneric:    return "Generic";
        case kAhBotClassRecipe:     return "Recipe";
        case kAhBotClassQuiver:     return "Quiver";
        case kAhBotClassQuest:      return "Quest";
        case kAhBotClassKey:        return "Key";
        case kAhBotClassMisc:       return "Misc";
        default:                    return nullptr;
    }
}

inline char const* AhBotQualityName(uint32_t quality)
{
    switch (quality)
    {
        case kAhBotQualityPoor:      return "Poor";
        case kAhBotQualityNormal:    return "Normal";
        case kAhBotQualityUncommon:  return "Uncommon";
        case kAhBotQualityRare:      return "Rare";
        case kAhBotQualityEpic:      return "Epic";
        case kAhBotQualityLegendary: return "Legendary";
        case kAhBotQualityArtifact:  return "Artifact";
        default:                     return nullptr;
    }
}

// Default listing proportion weights matching mod-ah-bot-plus mod_ahbot.conf.dist.
inline uint32_t AhBotDefaultListProportion(uint32_t itemClass, uint32_t quality)
{
    if (quality >= kAhBotMaxItemQuality)
        return 0;

    static constexpr uint32_t kDefaults[kAhBotMaxItemClass][kAhBotMaxItemQuality] = {
        // Poor, Normal, Uncommon, Rare, Epic, Legendary, Artifact
        {  0, 50, 10,  5,  0,  0, 0 }, // 0: Consumable
        {  0, 20,  8,  6,  3,  0, 0 }, // 1: Container
        {  0, 10, 40, 15,  4,  0, 0 }, // 2: Weapon
        {  0,  0,  0,  0,  0,  0, 0 }, // 3: Gem (unused in vanilla)
        { 10, 20, 50, 25,  8,  0, 0 }, // 4: Armor
        {  0, 10,  0,  0,  0,  0, 0 }, // 5: Reagent
        {  0, 10,  6,  4,  2,  0, 0 }, // 6: Projectile
        {  0, 80, 15,  8,  2,  0, 0 }, // 7: TradeGood
        { 10, 10, 10, 10, 10, 10, 0 }, // 8: Generic
        {  0, 30, 40, 15,  5,  0, 0 }, // 9: Recipe
        {  0,  0,  0,  0,  0,  0, 0 }, // 10: Money
        {  0, 10,  8,  3,  0,  0, 0 }, // 11: Quiver
        {  0, 25,  3,  2,  1,  0, 0 }, // 12: Quest
        {  0,  5,  2,  0,  0,  0, 0 }, // 13: Key
        {  0,  0,  0,  0,  0,  0, 0 }, // 14: Permanent
        {  0,  2,  2,  1,  1,  0, 0 }, // 15: Misc
    };

    if (itemClass >= kAhBotMaxItemClass)
        return 0;
    return kDefaults[itemClass][quality];
}

// Librams and Pristine Black Diamond have zero buy/sell price in item_template
// and are explicitly allowed past the price and misc checks in plus.
inline bool AhBotIsIncludeItemIdException(uint32_t itemId)
{
    switch (itemId)
    {
        case 11732:
        case 11733:
        case 11734:
        case 11736:
        case 11737:
        case 18332:
        case 18333:
        case 18334:
        case 18335:
            return true;
        default:
            return false;
    }
}

namespace detail
{

inline std::string TrimToken(std::string_view sv)
{
    size_t start = 0;
    while (start < sv.size() && std::isspace(static_cast<unsigned char>(sv[start])))
        ++start;
    size_t end = sv.size();
    while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1])))
        --end;
    return std::string(sv.substr(start, end - start));
}

inline bool ParseUint32Token(std::string const& tok, uint32_t& out)
{
    if (tok.empty())
        return false;
    for (unsigned char c : tok)
    {
        if (!std::isdigit(c))
            return false;
    }
    char* end = nullptr;
    unsigned long val = std::strtoul(tok.c_str(), &end, 10);
    if (end == tok.c_str() || *end != '\0' || val > UINT32_MAX)
        return false;
    out = static_cast<uint32_t>(val);
    return true;
}

} // namespace detail

// Parses comma-separated numbers and inclusive ranges (e.g. "2589,3200-3202").
inline void AhBotParseNumberListToSet(std::set<uint32_t>& out, std::string const& input)
{
    std::stringstream ss(input);
    std::string segment;
    while (std::getline(ss, segment, ','))
    {
        std::string tok = detail::TrimToken(segment);
        if (tok.empty())
            continue;
        size_t dash = tok.find('-');
        if (dash != std::string::npos)
        {
            uint32_t left = 0;
            uint32_t right = 0;
            if (!detail::ParseUint32Token(detail::TrimToken(std::string_view(tok).substr(0, dash)), left) ||
                !detail::ParseUint32Token(detail::TrimToken(std::string_view(tok).substr(dash + 1)), right))
                continue;
            if (left > right || (right - left) > 1000000)
                continue;
            for (uint32_t id = left; id <= right; ++id)
            {
                if (id > 0)
                    out.insert(id);
                if (id == UINT32_MAX)
                    break;
            }
        }
        else
        {
            uint32_t id = 0;
            if (detail::ParseUint32Token(tok, id) && id > 0)
                out.insert(id);
        }
    }
}

// Parses comma-separated "itemID:value" pairs (e.g. "2589:10,4306:10").
inline void AhBotParseItemValuePairs(std::unordered_map<uint32_t, uint32_t>& out, std::string const& input)
{
    std::stringstream ss(input);
    std::string segment;
    while (std::getline(ss, segment, ','))
    {
        std::string tok = detail::TrimToken(segment);
        size_t colon = tok.find(':');
        if (colon == std::string::npos)
            continue;
        uint32_t itemId = 0;
        uint32_t value = 0;
        if (!detail::ParseUint32Token(detail::TrimToken(std::string_view(tok).substr(0, colon)), itemId) ||
            !detail::ParseUint32Token(detail::TrimToken(std::string_view(tok).substr(colon + 1)), value))
            continue;
        if (itemId > 0 && value > 0)
            out.insert({ itemId, value });
    }
}

// Parses comma-separated "class:subclass" pairs where subclass may be "*".
inline void AhBotParseClassSubClassMap(
    std::map<uint32_t, std::unordered_set<uint32_t>>& out,
    std::string const& input,
    uint32_t wildcardLow = 0,
    uint32_t wildcardHigh = 20)
{
    std::stringstream ss(input);
    std::string segment;
    while (std::getline(ss, segment, ','))
    {
        std::string tok = detail::TrimToken(segment);
        size_t colon = tok.find(':');
        if (colon == std::string::npos)
            continue;
        uint32_t key = 0;
        if (!detail::ParseUint32Token(detail::TrimToken(std::string_view(tok).substr(0, colon)), key))
            continue;
        std::string valStr = detail::TrimToken(std::string_view(tok).substr(colon + 1));
        if (valStr == "*")
        {
            for (uint32_t sc = wildcardLow; sc <= wildcardHigh; ++sc)
                out[key].insert(sc);
        }
        else
        {
            uint32_t sc = 0;
            if (detail::ParseUint32Token(valStr, sc))
                out[key].insert(sc);
        }
    }
}

struct AhBotItemFilterConfig
{
    bool itemLevelRestrictEnabled = false;
    bool useCraftedItemForItemLevel = true;
    uint32_t minItemLevel = 0;
    uint32_t maxItemLevel = 999;
    std::set<uint32_t> itemLevelExceptions;

    bool itemIdRestrictEnabled = false;
    uint32_t minItemId = 0;
    uint32_t maxItemId = 200000;
    std::set<uint32_t> itemIdExceptions;

    bool equipLevelRestrictEnabled = true;
    uint32_t minRequiredLevel = 0;
    uint32_t maxRequiredLevel = 60;
    std::set<uint32_t> equipLevelExceptions;

    bool disabledItemTextFilter = true;
    bool disabledRecipeProducedFilterEnabled = false;
    std::map<uint32_t, std::unordered_set<uint32_t>> disabledRecipeProducedClassSubClasses;
    std::set<uint32_t> disabledItems;
};

struct AhBotItemFacts
{
    uint32_t itemId = 0;
    uint32_t itemClass = 0;
    uint32_t itemSubClass = 0;
    std::string_view name;
    uint32_t quality = 0;
    uint32_t flags = 0;
    uint32_t extraFlags = 0;
    uint32_t buyPrice = 0;
    uint32_t sellPrice = 0;
    uint32_t itemLevel = 0;
    uint32_t producedItemLevel = 0;
    bool hasProducedItem = false;
    uint32_t requiredLevel = 0;
    uint32_t containerSlots = 0;
    uint32_t bonding = 0;
    uint32_t duration = 0;
    uint32_t minMoneyLoot = 0;
    bool isTransmogClone = false;
    bool isObtainable = false;
    bool isSoldByUnlimitedVendor = false;
    bool isProducedByRecipe = false;
};

inline bool AhBotIsDisabledTextName(std::string_view name)
{
    static constexpr std::string_view kBadTokens[] = {
        "Test ",
        "TEST",
        "Deprecated",
        "Depricated",
        " Epic ",
        "]",
        "D'Sak",
        "(",
        "OLD",
        "PVP",
    };
    for (std::string_view token : kBadTokens)
    {
        if (name.find(token) != std::string_view::npos)
            return true;
    }
    return false;
}

inline bool AhBotIsItemCandidate(AhBotItemFacts const& item, AhBotItemFilterConfig const& cfg)
{
    if (item.itemId == 0)
        return false;

    // Hard invariants: no greys, no soulbound/quest-bound/account-bound,
    // vanilla AH categories only, not transmog clone, obtainable, and not sold
    // in unlimited stock by vendors.
    if (item.quality == kAhBotQualityPoor || item.quality >= kAhBotMaxItemQuality)
        return false;
    if (item.bonding == kAhBotBindWhenPickedUp ||
        item.bonding == kAhBotBindQuestItem ||
        item.bonding == kAhBotBindQuestItem1 ||
        item.bonding == kAhBotBindAccount)
        return false;
    if (AhBotCategoryName(item.itemClass) == nullptr)
        return false;
    if ((item.extraFlags & kAhBotItemExtraNotObtainable) != 0)
        return false;
    if (item.isTransmogClone)
        return false;
    if (!item.isObtainable || item.isSoldByUnlimitedVendor)
        return false;

    // Item level restriction (with recipe produced-item lookup option).
    if (cfg.itemLevelRestrictEnabled &&
        cfg.itemLevelExceptions.find(item.itemId) == cfg.itemLevelExceptions.end())
    {
        uint32_t levelToCompare = item.itemLevel;
        if (cfg.useCraftedItemForItemLevel &&
            item.itemClass == kAhBotClassRecipe &&
            item.hasProducedItem)
        {
            levelToCompare = item.producedItemLevel;
        }
        if (levelToCompare < cfg.minItemLevel || levelToCompare > cfg.maxItemLevel)
            return false;
    }

    // Item ID restriction.
    if (cfg.itemIdRestrictEnabled &&
        cfg.itemIdExceptions.find(item.itemId) == cfg.itemIdExceptions.end())
    {
        if (item.itemId < cfg.minItemId || item.itemId > cfg.maxItemId)
            return false;
    }

    // Use / equip level restriction (items with RequiredLevel == 0 are exempt).
    if (cfg.equipLevelRestrictEnabled &&
        cfg.equipLevelExceptions.find(item.itemId) == cfg.equipLevelExceptions.end())
    {
        if (item.requiredLevel > 0 &&
            (item.requiredLevel < cfg.minRequiredLevel || item.requiredLevel > cfg.maxRequiredLevel))
            return false;
    }

    // Explicit disabled item list.
    if (cfg.disabledItems.find(item.itemId) != cfg.disabledItems.end())
        return false;

    // Optional filter on recipe-produced class/subclass.
    if (cfg.disabledRecipeProducedFilterEnabled && item.isProducedByRecipe)
    {
        auto classIt = cfg.disabledRecipeProducedClassSubClasses.find(item.itemClass);
        if (classIt != cfg.disabledRecipeProducedClassSubClasses.end() &&
            classIt->second.find(item.itemSubClass) != classIt->second.end())
            return false;
    }

    // Include exceptions skip the remaining content/price heuristics below.
    if (AhBotIsIncludeItemIdException(item.itemId))
        return true;

    if ((item.flags & kAhBotItemFlagConjured) != 0)
        return false;
    if (item.minMoneyLoot > 0)
        return false;
    if (item.duration > 0)
        return false;
    if (item.itemClass == kAhBotClassContainer && item.containerSlots == 0)
        return false;
    if (item.itemClass == kAhBotClassRecipe &&
        item.itemSubClass == kAhBotSubclassRecipeBook &&
        item.quality <= kAhBotQualityNormal)
        return false;
    if (cfg.disabledItemTextFilter && AhBotIsDisabledTextName(item.name))
        return false;

    bool const isEnchantingTradeGood =
        (item.itemClass == kAhBotClassTradeGoods && item.itemSubClass == kAhBotSubclassTradeGoodsEnchanting);
    bool const isItemEnhancement =
        (item.itemClass == kAhBotClassConsumable && item.itemSubClass == kAhBotSubclassConsumableEnhancement);
    bool const hasNoPrice = (item.sellPrice == 0 && item.buyPrice == 0);
    if (hasNoPrice && !isItemEnhancement && !isEnchantingTradeGood)
        return false;

    return true;
}

class AhBotItemPoolTable
{
public:
    void Clear()
    {
        for (auto& byQual : buckets_)
            for (auto& vec : byQual)
                vec.clear();
        for (auto& byQual : prefixWeights_)
            for (auto& vec : byQual)
                vec.clear();
        candidateSet_.clear();
        proportionEntries_.clear();
        totalProportionWeight_ = 0;
    }

    void AddCandidate(uint32_t itemId, uint32_t itemClass, uint32_t quality)
    {
        if (itemId == 0 || itemClass >= kAhBotMaxItemClass || quality >= kAhBotMaxItemQuality)
            return;
        if (!candidateSet_.insert(itemId).second)
            return;
        buckets_[itemClass][quality].push_back(itemId);
    }

    void BuildProportions(
        std::array<std::array<uint32_t, kAhBotMaxItemQuality>, kAhBotMaxItemClass> const& proportions,
        std::unordered_map<uint32_t, uint32_t> const& multipliedItemIds)
    {
        proportionEntries_.clear();
        totalProportionWeight_ = 0;

        for (uint32_t cls = 0; cls < kAhBotMaxItemClass; ++cls)
        {
            if (AhBotCategoryName(cls) == nullptr)
                continue;
            for (uint32_t qual = 0; qual < kAhBotMaxItemQuality; ++qual)
            {
                auto const& items = buckets_[cls][qual];
                auto& weights = prefixWeights_[cls][qual];
                weights.clear();
                if (items.empty())
                    continue;

                uint64_t runningItemWeight = 0;
                weights.reserve(items.size());
                for (uint32_t itemId : items)
                {
                    uint32_t mult = 1;
                    auto it = multipliedItemIds.find(itemId);
                    if (it != multipliedItemIds.end() && it->second > 1)
                        mult = it->second;
                    runningItemWeight += mult;
                    weights.push_back(runningItemWeight);
                }

                uint32_t prop = proportions[cls][qual];
                if (prop > 0)
                {
                    totalProportionWeight_ += prop;
                    proportionEntries_.push_back({ cls, qual, totalProportionWeight_ });
                }
            }
        }
    }

    // Selects a weighted (category, quality) bucket, then an item within that
    // bucket weighted by ListMultipliedItemIDs. nextRand(limit) must return a
    // uniform integer in [0, limit - 1]. Returns 0 if no bucket is pickable.
    template <typename RngFn>
    uint32_t Pick(RngFn&& nextRand) const
    {
        if (totalProportionWeight_ == 0 || proportionEntries_.empty())
            return 0;

        uint64_t const propRoll = nextRand(totalProportionWeight_);
        auto propIt = std::upper_bound(
            proportionEntries_.begin(),
            proportionEntries_.end(),
            propRoll,
            [](uint64_t val, ProportionEntry const& entry) {
                return val < entry.cumulativeWeight;
            });
        if (propIt == proportionEntries_.end())
            propIt = std::prev(proportionEntries_.end());

        auto const& items = buckets_[propIt->itemClass][propIt->quality];
        auto const& weights = prefixWeights_[propIt->itemClass][propIt->quality];
        if (items.empty() || weights.empty() || weights.back() == 0)
            return 0;

        uint64_t const itemRoll = nextRand(weights.back());
        auto itemIt = std::upper_bound(weights.begin(), weights.end(), itemRoll);
        size_t const idx = static_cast<size_t>(std::distance(weights.begin(), itemIt));
        if (idx >= items.size())
            return items.back();
        return items[idx];
    }

    bool IsCandidate(uint32_t itemId) const
    {
        return candidateSet_.find(itemId) != candidateSet_.end();
    }

    size_t CandidateCount() const
    {
        return candidateSet_.size();
    }

    size_t BucketCount(uint32_t itemClass, uint32_t quality) const
    {
        if (itemClass >= kAhBotMaxItemClass || quality >= kAhBotMaxItemQuality)
            return 0;
        return buckets_[itemClass][quality].size();
    }

    uint64_t TotalProportionWeight() const
    {
        return totalProportionWeight_;
    }

    std::string GetStatus() const
    {
        std::ostringstream oss;
        oss << "candidates=" << candidateSet_.size()
            << " proportionWeight=" << totalProportionWeight_;
        for (uint32_t cls = 0; cls < kAhBotMaxItemClass; ++cls)
        {
            char const* catName = AhBotCategoryName(cls);
            if (!catName)
                continue;
            size_t catTotal = 0;
            std::ostringstream qualStr;
            bool firstQual = true;
            for (uint32_t q = 0; q < kAhBotMaxItemQuality; ++q)
            {
                size_t cnt = buckets_[cls][q].size();
                if (cnt == 0)
                    continue;
                catTotal += cnt;
                if (!firstQual)
                    qualStr << ',';
                firstQual = false;
                qualStr << AhBotQualityName(q) << '=' << cnt;
            }
            if (catTotal > 0)
                oss << " | " << catName << '(' << catTotal << ": " << qualStr.str() << ')';
        }
        return oss.str();
    }

private:
    struct ProportionEntry
    {
        uint32_t itemClass = 0;
        uint32_t quality = 0;
        uint64_t cumulativeWeight = 0;
    };

    std::array<std::array<std::vector<uint32_t>, kAhBotMaxItemQuality>, kAhBotMaxItemClass> buckets_{};
    std::array<std::array<std::vector<uint64_t>, kAhBotMaxItemQuality>, kAhBotMaxItemClass> prefixWeights_{};
    std::unordered_set<uint32_t> candidateSet_;
    std::vector<ProportionEntry> proportionEntries_;
    uint64_t totalProportionWeight_ = 0;
};

} // namespace ai
