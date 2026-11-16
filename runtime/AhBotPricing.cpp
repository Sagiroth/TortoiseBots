#include "AhBotPricing.h"

#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "Objects/ItemPrototype.h"
#include "Util.h"

#include <algorithm>

namespace TortoiseBots
{
namespace
{
ai::AhBotItemSpec MakeItemSpec(ItemPrototype const* proto)
{
    ai::AhBotItemSpec spec;
    spec.itemId = proto->ItemId;
    spec.itemClass = proto->Class;
    spec.itemSubClass = proto->SubClass;
    spec.quality = proto->Quality;
    spec.itemLevel = proto->ItemLevel;
    spec.sellPrice = proto->SellPrice;
    spec.maxStackSize = proto->GetMaxStackSize();
    spec.bagFamily = proto->BagFamily;
    spec.lockId = proto->LockID;
    return spec;
}

uint32_t RandRange(uint32_t lo, uint32_t hi)
{
    if (lo >= hi)
        return lo;
    return urand(lo, hi);
}
} // namespace

AhBotPricing& AhBotPricing::Instance()
{
    static AhBotPricing instance;
    return instance;
}

void AhBotPricing::EnsureLoaded() const
{
    if (!m_loaded)
        const_cast<AhBotPricing*>(this)->Reload();
}

void AhBotPricing::Reload()
{
    ai::AhBotPricingConfig cfg = ai::MakeDefaultVanillaPricingConfig();
    PlayerbotAIConfig const& aiCfg = sPlayerbotAIConfig;

    cfg.completeItemValueOverrideEnabled = aiCfg.ahBotCompleteItemValueOverrideEnabled;
    cfg.completeItemValueOverrideItems = ai::ParseItemValuePairs(aiCfg.ahBotCompleteItemValueOverrideItems);
    cfg.completeItemValueOverrideDoApplyBidVariations = aiCfg.ahBotCompleteItemValueOverrideDoApplyBidVariations;
    cfg.completeItemValueOverrideDoApplyBuyoutVariations = aiCfg.ahBotCompleteItemValueOverrideDoApplyBuyoutVariations;

    cfg.maxBuyoutPriceInCopper = aiCfg.ahBotMaxBuyoutPriceInCopper;
    cfg.buyoutVariationReducePercent = aiCfg.ahBotBuyoutVariationReducePercent;
    cfg.buyoutVariationAddPercent = aiCfg.ahBotBuyoutVariationAddPercent;
    cfg.bidVariationHighReducePercent = aiCfg.ahBotBidVariationHighReducePercent;
    cfg.bidVariationLowReducePercent = aiCfg.ahBotBidVariationLowReducePercent;
    cfg.buyoutBelowVendorVariationAddPercentEnabled = aiCfg.ahBotBuyoutBelowVendorVariationAddPercentEnabled;
    cfg.buyoutBelowVendorVariationAddPercent = aiCfg.ahBotBuyoutBelowVendorVariationAddPercent;
    cfg.buyerAcceptablePriceModifier = aiCfg.ahBotBuyerAcceptablePriceModifier;

    cfg.useItemSellPriceIfHigherThanPriceMinimumCenterBase = aiCfg.ahBotPriceMinimumCenterBaseUseItemSellPriceIfHigher;
    cfg.priceMinimumCenterBaseOverrides = ai::ParseItemValuePairs(aiCfg.ahBotPriceMinimumCenterBaseOverrideItems);
    cfg.priceMinimumCenterBase[ITEM_CLASS_CONSUMABLE] = aiCfg.ahBotPriceMinimumCenterBaseConsumable;
    cfg.priceMinimumCenterBase[ITEM_CLASS_CONTAINER] = aiCfg.ahBotPriceMinimumCenterBaseContainer;
    cfg.priceMinimumCenterBase[ITEM_CLASS_WEAPON] = aiCfg.ahBotPriceMinimumCenterBaseWeapon;
    cfg.priceMinimumCenterBase[ITEM_CLASS_ARMOR] = aiCfg.ahBotPriceMinimumCenterBaseArmor;
    cfg.priceMinimumCenterBase[ITEM_CLASS_REAGENT] = aiCfg.ahBotPriceMinimumCenterBaseReagent;
    cfg.priceMinimumCenterBase[ITEM_CLASS_PROJECTILE] = aiCfg.ahBotPriceMinimumCenterBaseProjectile;
    cfg.priceMinimumCenterBase[ITEM_CLASS_TRADE_GOODS] = aiCfg.ahBotPriceMinimumCenterBaseTradeGood;
    cfg.priceMinimumCenterBase[ITEM_CLASS_GENERIC] = aiCfg.ahBotPriceMinimumCenterBaseGeneric;
    cfg.priceMinimumCenterBase[ITEM_CLASS_RECIPE] = aiCfg.ahBotPriceMinimumCenterBaseRecipe;
    cfg.priceMinimumCenterBase[ITEM_CLASS_QUIVER] = aiCfg.ahBotPriceMinimumCenterBaseQuiver;
    cfg.priceMinimumCenterBase[ITEM_CLASS_QUEST] = aiCfg.ahBotPriceMinimumCenterBaseQuest;
    cfg.priceMinimumCenterBase[ITEM_CLASS_KEY] = aiCfg.ahBotPriceMinimumCenterBaseKey;
    cfg.priceMinimumCenterBase[ITEM_CLASS_JUNK] = aiCfg.ahBotPriceMinimumCenterBaseMisc;

    cfg.advancedPricingEnabled[static_cast<size_t>(ai::AhBotAdvancedGroup::ConsumablePotion)] = aiCfg.ahBotAdvancedPricingConsumablePotionEnabled;
    cfg.advancedPricingEnabled[static_cast<size_t>(ai::AhBotAdvancedGroup::ConsumableElixir)] = aiCfg.ahBotAdvancedPricingConsumableElixirEnabled;
    cfg.advancedPricingEnabled[static_cast<size_t>(ai::AhBotAdvancedGroup::ConsumableFlask)] = aiCfg.ahBotAdvancedPricingConsumableFlaskEnabled;
    cfg.advancedPricingEnabled[static_cast<size_t>(ai::AhBotAdvancedGroup::TradeGoodCloth)] = aiCfg.ahBotAdvancedPricingTradeGoodClothEnabled;
    cfg.advancedPricingEnabled[static_cast<size_t>(ai::AhBotAdvancedGroup::TradeGoodHerb)] = aiCfg.ahBotAdvancedPricingTradeGoodHerbEnabled;
    cfg.advancedPricingEnabled[static_cast<size_t>(ai::AhBotAdvancedGroup::TradeGoodMetalStone)] = aiCfg.ahBotAdvancedPricingTradeGoodMetalStoneEnabled;
    cfg.advancedPricingEnabled[static_cast<size_t>(ai::AhBotAdvancedGroup::TradeGoodLeather)] = aiCfg.ahBotAdvancedPricingTradeGoodLeatherEnabled;
    cfg.advancedPricingEnabled[static_cast<size_t>(ai::AhBotAdvancedGroup::TradeGoodEnchanting)] = aiCfg.ahBotAdvancedPricingTradeGoodEnchantingEnabled;
    cfg.advancedPricingEnabled[static_cast<size_t>(ai::AhBotAdvancedGroup::TradeGoodElemental)] = aiCfg.ahBotAdvancedPricingTradeGoodElementalEnabled;
    cfg.advancedPricingEnabled[static_cast<size_t>(ai::AhBotAdvancedGroup::TradeGoodMeat)] = aiCfg.ahBotAdvancedPricingTradeGoodMeatEnabled;
    cfg.advancedPricingEnabled[static_cast<size_t>(ai::AhBotAdvancedGroup::MiscJunk)] = aiCfg.ahBotAdvancedPricingMiscJunkEnabled;
    cfg.advancedPricingEnabled[static_cast<size_t>(ai::AhBotAdvancedGroup::MiscMount)] = aiCfg.ahBotAdvancedPricingMiscMountEnabled;
    cfg.advancedPricingEnabled[static_cast<size_t>(ai::AhBotAdvancedGroup::MiscPet)] = aiCfg.ahBotAdvancedPricingMiscPetEnabled;

    cfg.priceMultiplierCategory[ITEM_CLASS_CONSUMABLE] = aiCfg.ahBotPriceMultiplierCategoryConsumable;
    cfg.priceMultiplierCategory[ITEM_CLASS_CONTAINER] = aiCfg.ahBotPriceMultiplierCategoryContainer;
    cfg.priceMultiplierCategory[ITEM_CLASS_WEAPON] = aiCfg.ahBotPriceMultiplierCategoryWeapon;
    cfg.priceMultiplierCategory[ITEM_CLASS_ARMOR] = aiCfg.ahBotPriceMultiplierCategoryArmor;
    cfg.priceMultiplierCategory[ITEM_CLASS_REAGENT] = aiCfg.ahBotPriceMultiplierCategoryReagent;
    cfg.priceMultiplierCategory[ITEM_CLASS_PROJECTILE] = aiCfg.ahBotPriceMultiplierCategoryProjectile;
    cfg.priceMultiplierCategory[ITEM_CLASS_TRADE_GOODS] = aiCfg.ahBotPriceMultiplierCategoryTradeGood;
    cfg.priceMultiplierCategory[ITEM_CLASS_GENERIC] = aiCfg.ahBotPriceMultiplierCategoryGeneric;
    cfg.priceMultiplierCategory[ITEM_CLASS_RECIPE] = aiCfg.ahBotPriceMultiplierCategoryRecipe;
    cfg.priceMultiplierCategory[ITEM_CLASS_QUIVER] = aiCfg.ahBotPriceMultiplierCategoryQuiver;
    cfg.priceMultiplierCategory[ITEM_CLASS_QUEST] = aiCfg.ahBotPriceMultiplierCategoryQuest;
    cfg.priceMultiplierCategory[ITEM_CLASS_KEY] = aiCfg.ahBotPriceMultiplierCategoryKey;
    cfg.priceMultiplierCategory[ITEM_CLASS_JUNK] = aiCfg.ahBotPriceMultiplierCategoryMisc;

    cfg.priceMultiplierQuality[ITEM_QUALITY_POOR] = aiCfg.ahBotPriceMultiplierQualityPoor;
    cfg.priceMultiplierQuality[ITEM_QUALITY_NORMAL] = aiCfg.ahBotPriceMultiplierQualityNormal;
    cfg.priceMultiplierQuality[ITEM_QUALITY_UNCOMMON] = aiCfg.ahBotPriceMultiplierQualityUncommon;
    cfg.priceMultiplierQuality[ITEM_QUALITY_RARE] = aiCfg.ahBotPriceMultiplierQualityRare;
    cfg.priceMultiplierQuality[ITEM_QUALITY_EPIC] = aiCfg.ahBotPriceMultiplierQualityEpic;
    cfg.priceMultiplierQuality[ITEM_QUALITY_LEGENDARY] = aiCfg.ahBotPriceMultiplierQualityLegendary;
    cfg.priceMultiplierQuality[ITEM_QUALITY_ARTIFACT] = aiCfg.ahBotPriceMultiplierQualityArtifact;

    cfg.priceMultiplierItemLevelCategory[ITEM_CLASS_CONSUMABLE] = aiCfg.ahBotPriceMultiplierItemLevelCategoryConsumable;
    cfg.priceMultiplierItemLevelCategory[ITEM_CLASS_CONTAINER] = aiCfg.ahBotPriceMultiplierItemLevelCategoryContainer;
    cfg.priceMultiplierItemLevelCategory[ITEM_CLASS_WEAPON] = aiCfg.ahBotPriceMultiplierItemLevelCategoryWeapon;
    cfg.priceMultiplierItemLevelCategory[ITEM_CLASS_ARMOR] = aiCfg.ahBotPriceMultiplierItemLevelCategoryArmor;
    cfg.priceMultiplierItemLevelCategory[ITEM_CLASS_REAGENT] = aiCfg.ahBotPriceMultiplierItemLevelCategoryReagent;
    cfg.priceMultiplierItemLevelCategory[ITEM_CLASS_PROJECTILE] = aiCfg.ahBotPriceMultiplierItemLevelCategoryProjectile;
    cfg.priceMultiplierItemLevelCategory[ITEM_CLASS_TRADE_GOODS] = aiCfg.ahBotPriceMultiplierItemLevelCategoryTradeGood;
    cfg.priceMultiplierItemLevelCategory[ITEM_CLASS_GENERIC] = aiCfg.ahBotPriceMultiplierItemLevelCategoryGeneric;
    cfg.priceMultiplierItemLevelCategory[ITEM_CLASS_RECIPE] = aiCfg.ahBotPriceMultiplierItemLevelCategoryRecipe;
    cfg.priceMultiplierItemLevelCategory[ITEM_CLASS_QUIVER] = aiCfg.ahBotPriceMultiplierItemLevelCategoryQuiver;
    cfg.priceMultiplierItemLevelCategory[ITEM_CLASS_QUEST] = aiCfg.ahBotPriceMultiplierItemLevelCategoryQuest;
    cfg.priceMultiplierItemLevelCategory[ITEM_CLASS_KEY] = aiCfg.ahBotPriceMultiplierItemLevelCategoryKey;
    cfg.priceMultiplierItemLevelCategory[ITEM_CLASS_JUNK] = aiCfg.ahBotPriceMultiplierItemLevelCategoryMisc;

    for (uint32_t c = 0; c < ai::kAhBotMaxItemClass; ++c)
        for (uint32_t q = 0; q < MAX_ITEM_QUALITY; ++q)
            cfg.priceMultiplierCategoryQuality[c][q] = aiCfg.ahBotPriceMultiplierCategoryQuality[c][q];

    for (uint32_t q = 0; q < MAX_ITEM_QUALITY; ++q)
    {
        cfg.priceMultiplierCategoryMountQuality[q] = aiCfg.ahBotPriceMultiplierCategoryMountQuality[q];
        cfg.priceMultiplierCategoryPetQuality[q] = aiCfg.ahBotPriceMultiplierCategoryPetQuality[q];
    }

    cfg.listingStackRandomRatio[ITEM_CLASS_CONSUMABLE] = aiCfg.ahBotListingStackRandomRatioConsumable;
    cfg.listingStackRandomRatio[ITEM_CLASS_CONTAINER] = aiCfg.ahBotListingStackRandomRatioContainer;
    cfg.listingStackRandomRatio[ITEM_CLASS_WEAPON] = aiCfg.ahBotListingStackRandomRatioWeapon;
    cfg.listingStackRandomRatio[ITEM_CLASS_ARMOR] = aiCfg.ahBotListingStackRandomRatioArmor;
    cfg.listingStackRandomRatio[ITEM_CLASS_REAGENT] = aiCfg.ahBotListingStackRandomRatioReagent;
    cfg.listingStackRandomRatio[ITEM_CLASS_PROJECTILE] = aiCfg.ahBotListingStackRandomRatioProjectile;
    cfg.listingStackRandomRatio[ITEM_CLASS_TRADE_GOODS] = aiCfg.ahBotListingStackRandomRatioTradeGood;
    cfg.listingStackRandomRatio[ITEM_CLASS_GENERIC] = aiCfg.ahBotListingStackRandomRatioGeneric;
    cfg.listingStackRandomRatio[ITEM_CLASS_RECIPE] = aiCfg.ahBotListingStackRandomRatioRecipe;
    cfg.listingStackRandomRatio[ITEM_CLASS_QUIVER] = aiCfg.ahBotListingStackRandomRatioQuiver;
    cfg.listingStackRandomRatio[ITEM_CLASS_QUEST] = aiCfg.ahBotListingStackRandomRatioQuest;
    cfg.listingStackRandomRatio[ITEM_CLASS_KEY] = aiCfg.ahBotListingStackRandomRatioKey;
    cfg.listingStackRandomRatio[ITEM_CLASS_JUNK] = aiCfg.ahBotListingStackRandomRatioMisc;

    cfg.listingStackRandomStackIncrement[ITEM_CLASS_CONSUMABLE] = aiCfg.ahBotListingStackRandomStackIncrementConsumable;
    cfg.listingStackRandomStackIncrement[ITEM_CLASS_CONTAINER] = aiCfg.ahBotListingStackRandomStackIncrementContainer;
    cfg.listingStackRandomStackIncrement[ITEM_CLASS_WEAPON] = aiCfg.ahBotListingStackRandomStackIncrementWeapon;
    cfg.listingStackRandomStackIncrement[ITEM_CLASS_ARMOR] = aiCfg.ahBotListingStackRandomStackIncrementArmor;
    cfg.listingStackRandomStackIncrement[ITEM_CLASS_REAGENT] = aiCfg.ahBotListingStackRandomStackIncrementReagent;
    cfg.listingStackRandomStackIncrement[ITEM_CLASS_PROJECTILE] = aiCfg.ahBotListingStackRandomStackIncrementProjectile;
    cfg.listingStackRandomStackIncrement[ITEM_CLASS_TRADE_GOODS] = aiCfg.ahBotListingStackRandomStackIncrementTradeGood;
    cfg.listingStackRandomStackIncrement[ITEM_CLASS_GENERIC] = aiCfg.ahBotListingStackRandomStackIncrementGeneric;
    cfg.listingStackRandomStackIncrement[ITEM_CLASS_RECIPE] = aiCfg.ahBotListingStackRandomStackIncrementRecipe;
    cfg.listingStackRandomStackIncrement[ITEM_CLASS_QUIVER] = aiCfg.ahBotListingStackRandomStackIncrementQuiver;
    cfg.listingStackRandomStackIncrement[ITEM_CLASS_QUEST] = aiCfg.ahBotListingStackRandomStackIncrementQuest;
    cfg.listingStackRandomStackIncrement[ITEM_CLASS_KEY] = aiCfg.ahBotListingStackRandomStackIncrementKey;
    cfg.listingStackRandomStackIncrement[ITEM_CLASS_JUNK] = aiCfg.ahBotListingStackRandomStackIncrementMisc;

    cfg.listingStackMaxStackSize[ITEM_CLASS_CONSUMABLE] = aiCfg.ahBotListingStackMaxStackSizeConsumable;
    cfg.listingStackMaxStackSize[ITEM_CLASS_CONTAINER] = aiCfg.ahBotListingStackMaxStackSizeContainer;
    cfg.listingStackMaxStackSize[ITEM_CLASS_WEAPON] = aiCfg.ahBotListingStackMaxStackSizeWeapon;
    cfg.listingStackMaxStackSize[ITEM_CLASS_ARMOR] = aiCfg.ahBotListingStackMaxStackSizeArmor;
    cfg.listingStackMaxStackSize[ITEM_CLASS_REAGENT] = aiCfg.ahBotListingStackMaxStackSizeReagent;
    cfg.listingStackMaxStackSize[ITEM_CLASS_PROJECTILE] = aiCfg.ahBotListingStackMaxStackSizeProjectile;
    cfg.listingStackMaxStackSize[ITEM_CLASS_TRADE_GOODS] = aiCfg.ahBotListingStackMaxStackSizeTradeGood;
    cfg.listingStackMaxStackSize[ITEM_CLASS_GENERIC] = aiCfg.ahBotListingStackMaxStackSizeGeneric;
    cfg.listingStackMaxStackSize[ITEM_CLASS_RECIPE] = aiCfg.ahBotListingStackMaxStackSizeRecipe;
    cfg.listingStackMaxStackSize[ITEM_CLASS_QUIVER] = aiCfg.ahBotListingStackMaxStackSizeQuiver;
    cfg.listingStackMaxStackSize[ITEM_CLASS_QUEST] = aiCfg.ahBotListingStackMaxStackSizeQuest;
    cfg.listingStackMaxStackSize[ITEM_CLASS_KEY] = aiCfg.ahBotListingStackMaxStackSizeKey;
    cfg.listingStackMaxStackSize[ITEM_CLASS_JUNK] = aiCfg.ahBotListingStackMaxStackSizeMisc;

    m_config = std::move(cfg);
    m_loaded = true;
}

uint32_t AhBotPricing::StackSizeFor(ItemPrototype const* proto) const
{
    if (!proto)
        return 1;
    EnsureLoaded();
    return ai::GetStackSizeForItem(MakeItemSpec(proto), m_config, RandRange);
}

AhBotListingPrice AhBotPricing::PriceFor(ItemPrototype const* proto, uint32_t count) const
{
    if (!proto)
        return {};
    EnsureLoaded();
    ai::AhBotPricePair pair = ai::CalculateListingPrice(MakeItemSpec(proto), count, m_config, RandRange);
    return { pair.bid, pair.buyout };
}

uint64_t AhBotPricing::BuyerFairValuePerUnit(ItemPrototype const* proto) const
{
    if (!proto)
        return 0;
    EnsureLoaded();
    return ai::CalculateBuyerFairValuePerUnit(MakeItemSpec(proto), m_config, RandRange);
}
} // namespace TortoiseBots
