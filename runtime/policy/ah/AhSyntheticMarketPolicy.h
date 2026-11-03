#pragma once

// Pure policy for the synthetic AH supply and buyer engine (issues #88,
// #1180): the item eligibility, pricing, stack, override and buyer-bid
// decision rules used by AhMarketService. These rules used to exist twice -
// once in the service, once as a mock copy inside tools/test_synthetic_ah.cpp
// that stayed green when the service changed. They live here std-only so the
// test compiles the real rules; callers translate game state (ItemPrototype,
// config, override map, randomness, DB) into plain inputs.
// No core includes.

#include <algorithm>
#include <cstdint>

namespace ai
{
    // Synthetic listings are owned by nobody: no guid, no account. The gold
    // a synthetic sale burns is a pure economy sink, and a synthetic item
    // guid must never surface in a player's bags.
    inline constexpr uint32_t kAhSyntheticOwnerGuid = 0;
    inline constexpr uint32_t kAhSyntheticOwnerAccount = 0;

    // An override blacklists an item when either dimension is zeroed
    // (AhMarketService::IsItemBlacklisted).
    inline bool AhItemBlacklisted(uint32_t overrideValue, uint32_t overrideAddChance)
    {
        return overrideAddChance == 0 || overrideValue == 0;
    }

    // Override normalization for the ahbot_items command/DB path: the post
    // chance is a percentage (clamped to 100) and the amount window never
    // inverts (AhMarketService::SetItemOverride / ReloadOverrides).
    inline uint32_t AhClampedOverrideChance(uint32_t addChance)
    {
        return std::min<uint32_t>(100, addChance);
    }

    inline uint32_t AhOverrideMaxAmount(uint32_t minAmount, uint32_t maxAmount)
    {
        return std::max(minAmount, maxAmount);
    }

    // Core ItemPrototype constants the eligibility rule keys on (DBC-fixed;
    // ItemPrototype.h: BIND_WHEN_PICKED_UP = 1, BIND_QUEST_ITEM = 4,
    // ITEM_FLAG_LOOTABLE = 0x00000004).
    inline constexpr uint32_t kAhBindWhenPickedUp = 1;
    inline constexpr uint32_t kAhBindQuestItem = 4;
    inline constexpr uint32_t kAhItemFlagHasLoot = 0x00000004;

    // Synthetic supply eligibility (AhMarketService::IsItemEligible):
    // stackable, inside the quality/level caps, not blacklisted, never an
    // unlimited vendor item, and - unless forced by an override - not
    // soulbound-on-pickup, not a quest item, and without a loot window.
    inline bool AhItemEligible(bool blacklisted, bool forced, bool stackable,
        uint32_t quality, uint32_t maxQuality,
        uint32_t requiredLevel, uint32_t maxRequiredLevel,
        uint32_t itemLevel, uint32_t maxItemLevel,
        uint32_t bonding, uint32_t flags, bool unlimitedVendorStock = false)
    {
        if (!stackable || quality > maxQuality)
            return false;
        if (blacklisted)
            return false;
        if (unlimitedVendorStock)
            return false;
        if (forced)
            return true;
        if (requiredLevel > maxRequiredLevel || itemLevel > maxItemLevel)
            return false;
        if (bonding == kAhBindWhenPickedUp || bonding == kAhBindQuestItem)
            return false;
        if (flags & kAhItemFlagHasLoot)
            return false;
        return true;
    }

    // Quality price multiplier in percent of the base price
    // (AhMarketService::CalculatePrice; ItemQualities POOR..ARTIFACT).
    inline uint32_t AhQualityPriceMultiplier(uint32_t quality)
    {
        switch (quality)
        {
            case 0: return 50;    // ITEM_QUALITY_POOR
            case 1: return 100;   // ITEM_QUALITY_NORMAL
            case 2: return 200;   // ITEM_QUALITY_UNCOMMON
            case 3: return 400;   // ITEM_QUALITY_RARE
            case 4: return 800;   // ITEM_QUALITY_EPIC
            case 5: return 1600;  // ITEM_QUALITY_LEGENDARY
            case 6: return 3200;  // ITEM_QUALITY_ARTIFACT
            default: return 100;
        }
    }

    // Base (pre-variance) synthetic listing price: quality-scaled vendor
    // price with a buy/sell sanity rule (a buy price more than 5x the sell
    // price is discounted back to the sell-price scale), floored at 1 copper
    // and capped at 0x7fffffff so unit*stack math cannot overflow later.
    // Can return 0 for a zero-value poor item (no vendor price at all):
    // the caller must treat that as unpostable and return before drawing
    // any variance random number.
    inline uint32_t AhSyntheticBasePrice(uint32_t buyPrice, uint32_t sellPrice, uint32_t quality)
    {
        uint32_t qualityMultiplier = AhQualityPriceMultiplier(quality);

        uint64_t base = buyPrice;
        if (!base || (sellPrice && buyPrice / sellPrice > 5))
            base = uint64_t(sellPrice) * (quality <= 1 ? 4 : 5);
        if (!base)
            base = 1;

        return uint32_t(std::min<uint64_t>(base * qualityMultiplier / 100, 0x7fffffffULL));
    }

    // Variance applied to a base price given the caller's drawn delta in
    // percent (production: urand(0, variance * 2) - variance); floored at
    // 1 copper and capped at 0x7fffffff (AhMarketService::ApplyVariance).
    inline uint32_t AhVariedPrice(uint32_t price, int32_t deltaPercent)
    {
        int64_t varied = int64_t(price) + (int64_t(price) * deltaPercent / 100);
        if (varied <= 0)
            return 1;
        return (uint32_t)std::min<int64_t>(varied, 0x7fffffffLL);
    }

    // Stack size for one synthetic listing: bounded by stock, the
    // prototype's stackable max and what a single auction may cost; the
    // override's min/max window then widens/tightens (0 = unbounded on that
    // side). 0 means "nothing to post" (AhMarketService::CalculateStack).
    inline uint32_t AhStackCount(bool stackable, uint32_t stackMax, uint32_t stockCount, uint32_t unitPrice,
        uint32_t overrideMinAmount, uint32_t overrideMaxAmount)
    {
        if (!stackable || !stockCount || !unitPrice)
            return 0;

        uint32_t maxAffordable = uint32_t(0x7fffffffULL / unitPrice);
        uint32_t count = std::min<uint32_t>(stockCount, std::min<uint32_t>(stackMax, maxAffordable));

        if (overrideMinAmount)
            count = std::max(count, overrideMinAmount);
        if (overrideMaxAmount)
            count = std::min(count, overrideMaxAmount);

        return count ? count : 1;
    }

    // A bot never bids on its own listing, a same-account listing, or one it
    // already holds the high bid on (AhMarketService::BuyAuctionCandidate).
    inline bool AhBuyerExcluded(uint32_t auctionOwner, uint32_t auctionOwnerAccount,
        uint32_t currentBidder, uint32_t buyerGuidLow, uint32_t buyerAccountId)
    {
        return auctionOwner == buyerGuidLow ||
               auctionOwnerAccount == buyerAccountId ||
               currentBidder == buyerGuidLow;
    }

    // Buyer fair value: the synthetic base unit price blended with the live
    // AH median when one exists, scaled by the configured buy value percent.
    inline uint64_t AhBuyerWillingness(uint32_t baseUnitPrice, uint32_t medianUnitPrice,
        uint32_t count, uint32_t buyValuePercent)
    {
        uint32_t fairUnit = baseUnitPrice;
        if (medianUnitPrice > 0)
            fairUnit = (baseUnitPrice + medianUnitPrice) / 2;

        return (uint64_t(fairUnit) * count * buyValuePercent) / 100;
    }

    // Buyer bid decision: true with targetPrice = buyout (when worth it) or
    // the next minimum bid, false when neither is worth it
    // (AhMarketService::BuyAuctionCandidate). `outbid` is the 5% increment
    // (AuctionEntry::GetAuctionOutBid).
    inline bool AhBuyerTargetPrice(uint64_t willingness, uint32_t buyout, uint32_t currentBid,
        uint32_t outbid, uint32_t startbid, uint32_t& targetPrice)
    {
        uint32_t nextBid = currentBid ? (currentBid + outbid) : startbid;

        bool canBuyout = (buyout > 0 && willingness >= buyout);
        bool canBid = (!canBuyout && willingness >= nextBid && (buyout == 0 || nextBid < buyout));

        if (!canBuyout && !canBid)
            return false;

        targetPrice = canBuyout ? buyout : nextBid;
        return true;
    }

    // A listing with an active bid carries committed currency: it must run
    // its natural course to victory or outbid refund, so it is protected
    // from early expiration even during admin rebuild-all
    // (AhMarketService::StepExpire).
    inline bool AhListingProtectedFromExpire(uint32_t bid)
    {
        return bid != 0;
    }

    // Synthetic listing identification: owned by nobody (guid 0) or tracked
    // as a posted synthetic auction id (AhMarketService::IsSyntheticAuction).
    inline bool AhIsSyntheticAuction(uint32_t ownerGuidLow, bool trackedSyntheticId)
    {
        return ownerGuidLow == kAhSyntheticOwnerGuid || trackedSyntheticId;
    }
} // namespace ai
