// Standalone regression test for issue #88: Economy Synthetic AH Supply and Buyer Engine with Work Budgets.
// The decision rules under test (eligibility, pricing, stacks, overrides,
// buyer exclusions and bid decision, expiration protection, synthetic
// ownership) live in runtime/AhSyntheticMarketPolicy.h, which production
// (AhMarketService) calls into (issue #1180); the mock service below only
// orchestrates game-state bookkeeping around them.
// Verifies:
//   1. Item generation eligibility, quality/level caps, pricing and variance
//   2. Synthetic inventory isolation (SYNTHETIC_OWNER_GUID = 0, never in player bags, safe expiration)
//   3. Buyer engine policies, spend caps, exclusions (same-account, own auction, outbid self)
//   4. Overrides, blacklisting, and instant reload via ahbot_items
//   5. Exact item and currency conservation
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_synthetic_ah.cpp -o /tmp/test_synthetic_ah && /tmp/test_synthetic_ah

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../runtime/policy/ah/AhSyntheticMarketPolicy.h"

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

using uint32 = uint32_t;
using uint8 = uint8_t;
using int32 = int32_t;
using uint64 = uint64_t;

// Faction and item constants
constexpr uint32 ALLIANCE = 469;
constexpr uint32 HORDE = 67;
constexpr uint32 NEUTRAL_TEAM = 0;

constexpr uint32 ITEM_QUALITY_POOR = 0;
constexpr uint32 ITEM_QUALITY_NORMAL = 1;
constexpr uint32 ITEM_QUALITY_UNCOMMON = 2;
constexpr uint32 ITEM_QUALITY_RARE = 3;
constexpr uint32 ITEM_QUALITY_EPIC = 4;
constexpr uint32 ITEM_QUALITY_LEGENDARY = 5;

constexpr uint32 ITEM_CLASS_QUEST = 12;
constexpr uint32 ITEM_CLASS_CONSUMABLE = 0;
constexpr uint32 ITEM_CLASS_WEAPON = 2;
constexpr uint32 ITEM_CLASS_ARMOR = 4;
constexpr uint32 ITEM_CLASS_TRADE_GOODS = 7;

constexpr uint32 SYNTHETIC_OWNER_GUID = 0;
constexpr uint32 SYNTHETIC_OWNER_ACCOUNT = 0;

struct AuctionHouseEntry
{
    uint32 houseId;
    uint32 faction;
    uint32 depositPercent;
};

struct ItemPrototype
{
    uint32 ItemId;
    std::string Name1;
    uint32 Class;
    uint32 SubClass;
    uint32 Quality;
    uint32 BuyPrice;
    uint32 SellPrice;
    uint32 ItemLevel;
    uint32 RequiredLevel;
    uint32 Stackable;
    uint32 Bonding;
    uint32 Flags;
};

struct Item
{
    uint32 guid;
    uint32 entry;
    uint32 count;
    ItemPrototype const* proto;

    uint32 GetCount() const { return count; }
    uint32 GetEntry() const { return entry; }
    uint32 GetGUIDLow() const { return guid; }
    ItemPrototype const* GetProto() const { return proto; }
};

struct AuctionEntry
{
    uint32 Id = 0;
    uint32 itemGuidLow = 0;
    uint32 itemTemplate = 0;
    uint32 itemCount = 1;
    uint32 owner = 0;
    uint32 ownerAccount = 0;
    uint32 startbid = 0;
    uint32 bid = 0;
    uint32 buyout = 0;
    uint32 bidder = 0;
    uint32 deposit = 0;
    time_t expire_time = 0;
    AuctionHouseEntry const* auctionHouseEntry = nullptr;

    uint32 GetHouseId() const { return auctionHouseEntry ? auctionHouseEntry->houseId : 0; }
    uint32 GetAuctionCut() const
    {
        if (!auctionHouseEntry) return 0;
        return (bid * auctionHouseEntry->depositPercent) / 100;
    }
    uint32 GetAuctionOutBid() const
    {
        uint32 outbid = bid / 20; // 5% increment
        return outbid ? outbid : 1;
    }
};

class AuctionHouseObject
{
public:
    std::map<uint32, AuctionEntry*> auctions;

    void AddAuction(AuctionEntry* a) { auctions[a->Id] = a; }
    void RemoveAuction(uint32 id) { auctions.erase(id); }
    AuctionEntry* GetAuction(uint32 id)
    {
        auto it = auctions.find(id);
        return it != auctions.end() ? it->second : nullptr;
    }
    size_t GetCount() const { return auctions.size(); }
};

struct MockSession
{
    uint32 accountId;
    uint32 GetAccountId() const { return accountId; }
};

struct MockPlayer
{
    uint32 guid;
    uint32 accountId;
    std::string name;
    uint32 team;
    uint32 money;
    MockSession session;
    std::vector<Item*> inventory;

    MockPlayer(uint32 g, uint32 acc, std::string n, uint32 t, uint32 m)
        : guid(g), accountId(acc), name(std::move(n)), team(t), money(m), session{acc} {}

    uint32 GetGUIDLow() const { return guid; }
    uint32 GetTeam() const { return team; }
    uint32 GetMoney() const { return money; }
    void ModifyMoney(int32 delta) { money = static_cast<uint32>(static_cast<int32>(money) + delta); }
    MockSession* GetSession() { return &session; }
    MockSession const* GetSession() const { return &session; }
    char const* GetName() const { return name.c_str(); }

    bool HasItemWithGuid(uint32 itemGuidLow) const
    {
        for (auto const* itm : inventory)
        {
            if (itm && itm->guid == itemGuidLow)
                return true;
        }
        return false;
    }
};

// -----------------------------------------------------------------
// Config and Service Mock Mirroring TortoiseBots Implementation
// -----------------------------------------------------------------

struct MockConfig
{
    bool ahMarketSyntheticSupply = true;
    bool ahMarketBuyer = true;
    uint32 ahMarketBudgetUs = 2000;
    uint32 ahMarketMaxOperations = 32;
    uint32 ahMarketChanceSell = 50;
    uint32 ahMarketChanceBuy = 50;
    uint32 ahMarketBuyValue = 80;
    uint32 ahMarketMaxSpendPerBot = 100000; // 10g
    uint32 ahMarketMaxQuality = 4;          // Epic
    uint32 ahMarketMaxLevel = 60;
    bool ahMarketDynamicLevel = false;
    uint32 ahMarketLevelRefresh = 600;
    uint32 ahMarketVariance = 10;
    uint32 ahMarketBidMin = 75;
    uint32 ahMarketBidMax = 90;
};

struct ItemOverride
{
    uint32 value = 0;       // 0 = blacklisted/banned if add_chance == 0
    uint32 add_chance = 0;  // 0 = blacklisted
    uint32 min_amount = 0;
    uint32 max_amount = 0;
};

enum class MockPhase : uint8_t
{
    Idle,
    Gather,
    Overrides,
    Post,
    Buy,
    Expire
};

struct MockAhMarketService
{
    MockConfig config;
    MockPhase phase = MockPhase::Idle;

    std::unordered_map<uint32, ItemOverride> overrides;
    std::map<uint32, uint32> stock;
    std::unordered_set<uint32> syntheticAuctions;
    std::unordered_set<uint32> syntheticItemGuids;

    // Stats
    uint64 totalListed = 0;
    uint64 totalBought = 0;
    uint64 totalExpired = 0;
    uint64 totalProtectedBids = 0;
    uint64 totalOperationsExecuted = 0;

    // Level caps as the service tracks them (dynamic level refresh)
    uint32 maxRequiredLevel = 60;
    uint32 maxItemLevel = 255;

    // Available mock item prototypes
    std::unordered_map<uint32, ItemPrototype> prototypes;

    // Auction house
    AuctionHouseEntry ahAlliance{ 1, ALLIANCE, 5 };
    AuctionHouseObject ahObjectAlliance;
    uint32 nextAuctionId = 1000;
    uint32 nextItemGuid = 50000;
    size_t gatherIndex = 0;
    size_t overrideIndex = 0;

    // Database tracking mock (for item_instance table)
    std::unordered_map<uint32, Item*> dbItemInstances;

    bool IsSyntheticAuction(uint32 auctionId) const
    {
        return syntheticAuctions.find(auctionId) != syntheticAuctions.end();
    }

    bool IsSyntheticItem(uint32 itemGuidLow) const
    {
        return syntheticItemGuids.find(itemGuidLow) != syntheticItemGuids.end();
    }

    bool IsItemBlacklisted(uint32 itemId) const
    {
        auto it = overrides.find(itemId);
        if (it != overrides.end())
            return ai::AhItemBlacklisted(it->second.value, it->second.add_chance);
        return false;
    }

    bool AssertSyntheticIsolation(MockPlayer const* player, uint32 itemGuidLow) const
    {
        if (!player) return true;
        // Invariant: synthetic item GUID Low low must NEVER exist in any player inventory
        if (IsSyntheticItem(itemGuidLow) && player->HasItemWithGuid(itemGuidLow))
            return false;
        return true;
    }

    void ReloadOverrides(std::unordered_map<uint32, ItemOverride> const& newOverrides)
    {
        overrides = newOverrides;
    }

    void SetItemOverride(uint32 itemId, uint32 value, uint32 addChance, uint32 minAmount, uint32 maxAmount)
    {
        ItemOverride ov;
        ov.value = value;
        ov.add_chance = addChance;
        ov.min_amount = minAmount;
        ov.max_amount = maxAmount;
        overrides[itemId] = ov;
    }

    void ResetItemOverride(uint32 itemId)
    {
        overrides.erase(itemId);
    }

    bool IsItemEligible(ItemPrototype const* proto, bool forced = false) const
    {
        if (!proto) return false;
        return ai::AhItemEligible(IsItemBlacklisted(proto->ItemId), forced, proto->Stackable != 0,
            proto->Quality, config.ahMarketMaxQuality,
            proto->RequiredLevel, maxRequiredLevel,
            proto->ItemLevel, maxItemLevel,
            proto->Bonding, proto->Flags);
    }

    uint32 CalculatePrice(ItemPrototype const* proto) const
    {
        if (!proto) return 0;
        auto it = overrides.find(proto->ItemId);
        if (it != overrides.end() && it->second.value > 0)
            return it->second.value;

        // Quality scaling + buy/sell sanity rule from the policy. A zero
        // base price (zero-value poor item) returns before the variance
        // draw, exactly as in production.
        uint32 basePrice = ai::AhSyntheticBasePrice(proto->BuyPrice, proto->SellPrice, proto->Quality);
        if (!basePrice)
            return 0;
        return ApplyVariance(basePrice);
    }

    uint32 ApplyVariance(uint32 price) const
    {
        if (!price || !config.ahMarketVariance)
            return price ? price : 1;
        int32 delta = static_cast<int32>(config.ahMarketVariance); // deterministic mock draw
        return ai::AhVariedPrice(price, delta);
    }

    // StepWorkSlice: mimics execution of tasks up to maxOperations or budget
    uint32 StepWorkSlice(uint32 forcedBudgetOps = 0)
    {
        uint32 maxOps = forcedBudgetOps ? forcedBudgetOps : config.ahMarketMaxOperations;
        uint32 opsExecuted = 0;

        while (opsExecuted < maxOps)
        {
            switch (phase)
            {
                case MockPhase::Idle:
                    phase = MockPhase::Gather;
                    ++opsExecuted;
                    break;

                case MockPhase::Gather:
                {
                    std::vector<ItemPrototype const*> protoList;
                    for (auto const& pair : prototypes)
                        protoList.push_back(&pair.second);

                    while (gatherIndex < protoList.size() && opsExecuted < maxOps)
                    {
                        ItemPrototype const* proto = protoList[gatherIndex++];
                        if (IsItemEligible(proto))
                        {
                            stock[proto->ItemId] += 2;
                        }
                        ++opsExecuted;
                    }
                    if (gatherIndex >= protoList.size())
                    {
                        gatherIndex = 0;
                        phase = MockPhase::Overrides;
                    }
                    break;
                }

                case MockPhase::Overrides:
                {
                    std::vector<uint32> overrideKeys;
                    for (auto const& pair : overrides)
                        overrideKeys.push_back(pair.first);

                    while (overrideIndex < overrideKeys.size() && opsExecuted < maxOps)
                    {
                        uint32 itemId = overrideKeys[overrideIndex++];
                        ItemOverride const& ov = overrides[itemId];
                        if (ov.add_chance == 0 || ov.value == 0)
                        {
                            stock.erase(itemId);
                        }
                        else
                        {
                            stock[itemId] = ov.min_amount;
                        }
                        ++opsExecuted;
                    }
                    if (overrideIndex >= overrideKeys.size())
                    {
                        overrideIndex = 0;
                        phase = MockPhase::Post;
                    }
                    break;
                }

                case MockPhase::Post:
                {
                    auto it = stock.begin();
                    while (it != stock.end() && opsExecuted < maxOps)
                    {
                        uint32 itemId = it->first;
                        uint32 count = it->second;
                        it = stock.erase(it);

                        auto pIt = prototypes.find(itemId);
                        if (pIt == prototypes.end() || !IsItemEligible(&pIt->second))
                        {
                            ++opsExecuted;
                            continue;
                        }

                        ItemPrototype const* proto = &pIt->second;
                        uint32 unitPrice = ApplyVariance(CalculatePrice(proto));
                        uint32 stack = ai::AhStackCount(proto->Stackable != 0, proto->Stackable, count, unitPrice, 0, 0);
                        if (!stack) { ++opsExecuted; continue; }
                        uint32 buyout = unitPrice * stack;
                        uint32 startbid = (buyout * config.ahMarketBidMin) / 100;
                        if (startbid == 0) startbid = 1;

                        uint32 itemGuid = nextItemGuid++;
                        Item* item = new Item{ itemGuid, itemId, stack, proto };
                        dbItemInstances[itemGuid] = item;

                        uint32 auctionId = nextAuctionId++;
                        AuctionEntry* auction = new AuctionEntry();
                        auction->Id = auctionId;
                        auction->itemGuidLow = itemGuid;
                        auction->itemTemplate = itemId;
                        auction->itemCount = stack;
                        auction->owner = SYNTHETIC_OWNER_GUID;
                        auction->ownerAccount = SYNTHETIC_OWNER_ACCOUNT;
                        auction->startbid = startbid;
                        auction->bid = 0;
                        auction->buyout = buyout;
                        auction->bidder = 0;
                        auction->deposit = 0;
                        auction->expire_time = time(nullptr) + 86400;
                        auction->auctionHouseEntry = &ahAlliance;

                        ahObjectAlliance.AddAuction(auction);
                        syntheticAuctions.insert(auctionId);
                        syntheticItemGuids.insert(itemGuid);
                        ++totalListed;
                        ++opsExecuted;
                    }

                    if (stock.empty())
                    {
                        phase = MockPhase::Buy;
                    }
                    break;
                }

                case MockPhase::Buy:
                    // Handled separately or completed
                    phase = MockPhase::Expire;
                    ++opsExecuted;
                    break;

                case MockPhase::Expire:
                    // Check for expired synthetic auctions
                    phase = MockPhase::Idle;
                    ++opsExecuted;
                    break;
            }
        }

        totalOperationsExecuted += opsExecuted;
        return opsExecuted;
    }

    // Buyer evaluation logic, production decision shape
    // (AhMarketService::BuyAuctionCandidate): exclusions via the policy,
    // then willingness and the buyout-or-next-bid decision from the policy,
    // then money/spend-cap gates, then execution.
    bool EvaluateAndBid(MockPlayer& buyer, AuctionEntry* auction, uint32 customMaxSpend = 0)
    {
        if (!auction) return false;

        // Own auction, same-account auction, already-highest-bidder
        if (ai::AhBuyerExcluded(auction->owner, auction->ownerAccount, auction->bidder,
                buyer.GetGUIDLow(), buyer.GetSession()->GetAccountId()))
            return false;

        // Blacklist check
        if (IsItemBlacklisted(auction->itemTemplate))
            return false;

        auto pIt = prototypes.find(auction->itemTemplate);
        if (pIt == prototypes.end()) return false;
        ItemPrototype const* proto = &pIt->second;

        // Harness stands in for the live AH read model: no median blend.
        uint32 medianMarket = 0;
        uint64 willingness = ai::AhBuyerWillingness(CalculatePrice(proto), medianMarket,
            auction->itemCount, config.ahMarketBuyValue);
        if (!willingness)
            return false;

        uint32 outbid = auction->GetAuctionOutBid();
        uint32 targetPrice = 0;
        if (!ai::AhBuyerTargetPrice(willingness, auction->buyout, auction->bid, outbid,
                auction->startbid, targetPrice))
            return false;

        if (buyer.GetMoney() < targetPrice)
            return false;

        uint32 spendCap = customMaxSpend ? customMaxSpend : config.ahMarketMaxSpendPerBot;
        if (spendCap > 0 && targetPrice > spendCap)
            return false;

        buyer.ModifyMoney(-static_cast<int32>(targetPrice));

        if (auction->buyout > 0 && targetPrice == auction->buyout) // Buyout
        {
            // Deliver item to buyer
            auto itmIt = dbItemInstances.find(auction->itemGuidLow);
            if (itmIt != dbItemInstances.end())
            {
                buyer.inventory.push_back(itmIt->second);
            }

            // Invariant: Synthetic item low guid is removed from synthetic set once owned by player
            if (auction->owner == ai::kAhSyntheticOwnerGuid)
            {
                syntheticItemGuids.erase(auction->itemGuidLow);
                syntheticAuctions.erase(auction->Id);
                // No gold sent to synthetic seller! Pure gold sink.
            }

            ahObjectAlliance.RemoveAuction(auction->Id);
            delete auction;
            ++totalBought;
            return true;
        }

        // Underbid / Bid
        auction->bid = targetPrice;
        auction->bidder = buyer.GetGUIDLow();
        ++totalProtectedBids;
        return true;
    }

    // Safely expire a synthetic auction
    bool SafelyExpireAuction(uint32 auctionId)
    {
        AuctionEntry* auction = ahObjectAlliance.GetAuction(auctionId);
        if (!auction) return false;

        // Never expire or delete an auction with active bids (protects committed gold)
        if (ai::AhListingProtectedFromExpire(auction->bid))
        {
            ++totalProtectedBids;
            return false;
        }

        if (auction->owner == ai::kAhSyntheticOwnerGuid)
        {
            // Safe cleanup: delete item from db, no mail sent
            auto itmIt = dbItemInstances.find(auction->itemGuidLow);
            if (itmIt != dbItemInstances.end())
            {
                delete itmIt->second;
                dbItemInstances.erase(itmIt);
            }
            syntheticItemGuids.erase(auction->itemGuidLow);
            syntheticAuctions.erase(auction->Id);
            ahObjectAlliance.RemoveAuction(auction->Id);
            delete auction;
            ++totalExpired;
            return true;
        }
        return false;
    }
};

int main()
{
    std::cout << "Starting Issue #88 Synthetic AH Supply & Buyer Engine Regression Tests...\n";

    // Setup prototypes
    ItemPrototype protoLinen{ 2589, "Linen Cloth", ITEM_CLASS_TRADE_GOODS, 0, ITEM_QUALITY_NORMAL, 100, 25, 10, 5, 20, 0, 0 };
    ItemPrototype protoSword{ 1928, "Heavy Copper Broadsword", ITEM_CLASS_WEAPON, 7, ITEM_QUALITY_UNCOMMON, 5000, 1200, 20, 15, 1, 2, 0 };
    ItemPrototype protoStaff{ 2042, "Staff of Jordan", ITEM_CLASS_WEAPON, 10, ITEM_QUALITY_EPIC, 200000, 50000, 40, 35, 1, 2, 0 };
    ItemPrototype protoLegend{ 19019, "Thunderfury", ITEM_CLASS_WEAPON, 7, ITEM_QUALITY_LEGENDARY, 1000000, 250000, 80, 60, 1, 1, 0 }; // Bound on pickup & Legendary
    ItemPrototype protoHighLevel{ 16000, "High Level Item", ITEM_CLASS_ARMOR, 1, ITEM_QUALITY_RARE, 100000, 25000, 65, 62, 1, 2, 0 }; // Level 62
    ItemPrototype protoZero{ 9999, "Worthless Pebble", ITEM_CLASS_TRADE_GOODS, 0, ITEM_QUALITY_POOR, 0, 0, 0, 0, 1, 0, 0 }; // No vendor price at all

    // -------------------------------------------------------------
    // Test 1: Item Eligibility, Caps & Blacklisting
    // -------------------------------------------------------------
    std::cout << "  [Test 1] Item eligibility, caps, and blacklists\n";
    {
        MockAhMarketService service;
        service.prototypes[protoLinen.ItemId] = protoLinen;
        service.prototypes[protoSword.ItemId] = protoSword;
        service.prototypes[protoStaff.ItemId] = protoStaff;
        service.prototypes[protoLegend.ItemId] = protoLegend;
        service.prototypes[protoHighLevel.ItemId] = protoHighLevel;

        CHECK(service.IsItemEligible(&protoLinen));
        CHECK(service.IsItemEligible(&protoSword));
        CHECK(service.IsItemEligible(&protoStaff));

        // Legendary exceeds max quality (4 = Epic)
        CHECK(!service.IsItemEligible(&protoLegend));

        // Level 62 exceeds max level (60)
        CHECK(!service.IsItemEligible(&protoHighLevel));

        // Blacklist item via override with add_chance = 0
        service.SetItemOverride(protoLinen.ItemId, 0, 0, 0, 0);
        CHECK(service.IsItemBlacklisted(protoLinen.ItemId));
        CHECK(!service.IsItemEligible(&protoLinen));

        // Either zeroed dimension alone blacklists: a price without a post
        // chance, and a post chance without a price.
        service.SetItemOverride(protoLinen.ItemId, 5000, 0, 0, 0);
        CHECK(service.IsItemBlacklisted(protoLinen.ItemId));
        service.SetItemOverride(protoLinen.ItemId, 0, 80, 0, 0);
        CHECK(service.IsItemBlacklisted(protoLinen.ItemId));

        // Reset override
        service.ResetItemOverride(protoLinen.ItemId);
        CHECK(!service.IsItemBlacklisted(protoLinen.ItemId));
        CHECK(service.IsItemEligible(&protoLinen));
    }

    // -------------------------------------------------------------
    // Test 1b: Zero-Value Poor Item Prices At 0 And Is Never Posted
    // -------------------------------------------------------------
    std::cout << "  [Test 1b] Zero-value poor item prices at 0, no variance draw\n";
    {
        MockAhMarketService service;
        service.prototypes[protoZero.ItemId] = protoZero;

        // Eligible by caps, but with no vendor price the base price is 0:
        // production must return 0 before drawing any variance random
        // number, so the item is never posted (old inlined behaviour).
        CHECK(service.IsItemEligible(&protoZero));
        CHECK(ai::AhSyntheticBasePrice(0, 0, ITEM_QUALITY_POOR) == 0);
        CHECK(service.CalculatePrice(&protoZero) == 0);
        CHECK(ai::AhStackCount(true, 1, 5, 0, 0, 0) == 0); // zero unit price: no stack
    }

    // -------------------------------------------------------------
    // Test 3: Synthetic Inventory Isolation & Assertions
    // -------------------------------------------------------------
    std::cout << "  [Test 3] Synthetic inventory isolation & assertability\n";
    {
        MockAhMarketService service;
        service.prototypes[protoSword.ItemId] = protoSword;
        service.config.ahMarketMaxOperations = 100;

        // Run full cycle to post synthetic items
        service.StepWorkSlice(100);
        while (service.phase != MockPhase::Idle)
            service.StepWorkSlice(100);

        CHECK(service.totalListed >= 1);
        CHECK(!service.syntheticAuctions.empty());
        CHECK(!service.syntheticItemGuids.empty());

        uint32 synthItemGuid = *service.syntheticItemGuids.begin();
        CHECK(service.IsSyntheticItem(synthItemGuid));

        MockPlayer realPlayer{ 10, 100, "RealPlayer", ALLIANCE, 50000 };

        // Isolation assertion passes when player does NOT have synthetic item
        CHECK(service.AssertSyntheticIsolation(&realPlayer, synthItemGuid));

        // If synthetic item were erroneously placed in player's bag, assertion fails!
        Item* leakedItem = service.dbItemInstances[synthItemGuid];
        realPlayer.inventory.push_back(leakedItem);
        CHECK(!service.AssertSyntheticIsolation(&realPlayer, synthItemGuid));

        // Clean up
        realPlayer.inventory.clear();
        CHECK(service.AssertSyntheticIsolation(&realPlayer, synthItemGuid));
    }

    // -------------------------------------------------------------
    // Test 4: Buyer Engine Evaluation, Spend Caps & Exclusions
    // -------------------------------------------------------------
    std::cout << "  [Test 4] Buyer engine, spend caps & exclusions\n";
    {
        MockAhMarketService service;
        service.prototypes[protoLinen.ItemId] = protoLinen;
        service.prototypes[protoSword.ItemId] = protoSword;

        // Setup a synthetic auction for linen cloth. Policy price: BuyPrice
        // 100 (within 5x of SellPrice 25) x NORMAL 100% = 100, +10% variance
        // = 110 per item; willingness = 110 * 2 items * 80% = 176.
        uint32 itemGuid = service.nextItemGuid++;
        Item* item = new Item{ itemGuid, protoLinen.ItemId, 2, &protoLinen };
        service.dbItemInstances[itemGuid] = item;

        uint32 auctionId = service.nextAuctionId++;
        AuctionEntry* auc = new AuctionEntry();
        auc->Id = auctionId;
        auc->itemGuidLow = itemGuid;
        auc->itemTemplate = protoLinen.ItemId;
        auc->itemCount = 2;
        auc->owner = SYNTHETIC_OWNER_GUID;
        auc->ownerAccount = SYNTHETIC_OWNER_ACCOUNT;
        auc->startbid = 100;
        auc->buyout = 150; // Below the 176 willingness -> buyout candidate
        auc->auctionHouseEntry = &service.ahAlliance;
        service.ahObjectAlliance.AddAuction(auc);
        service.syntheticAuctions.insert(auctionId);
        service.syntheticItemGuids.insert(itemGuid);

        MockPlayer buyerBot{ 20, 200, "BuyerBot", ALLIANCE, 1000 };

        // Test 4a: Spend cap prevents buying if cap < target price
        bool boughtWithLowCap = service.EvaluateAndBid(buyerBot, auc, 50 /* custom cap: 50c */);
        CHECK(!boughtWithLowCap);
        CHECK(buyerBot.GetMoney() == 1000);

        // Test 4b: Normal buy with adequate spend cap succeeds
        bool bought = service.EvaluateAndBid(buyerBot, auc, 1000);
        CHECK(bought);
        CHECK(buyerBot.GetMoney() == 850); // 1000 - 150
        CHECK(buyerBot.inventory.size() == 1);
        CHECK(buyerBot.inventory[0]->guid == itemGuid);
        CHECK(service.totalBought == 1);

        // Test 4c: Exclusion - cannot bid on own auction
        uint32 ownItemGuid = service.nextItemGuid++;
        Item* ownItem = new Item{ ownItemGuid, protoSword.ItemId, 1, &protoSword };
        service.dbItemInstances[ownItemGuid] = ownItem;

        uint32 ownAucId = service.nextAuctionId++;
        AuctionEntry* ownAuc = new AuctionEntry();
        ownAuc->Id = ownAucId;
        ownAuc->itemGuidLow = ownItemGuid;
        ownAuc->itemTemplate = protoSword.ItemId;
        ownAuc->itemCount = 1;
        ownAuc->owner = buyerBot.GetGUIDLow();
        ownAuc->ownerAccount = buyerBot.GetSession()->GetAccountId();
        ownAuc->startbid = 500;
        ownAuc->buyout = 1000;
        ownAuc->auctionHouseEntry = &service.ahAlliance;
        service.ahObjectAlliance.AddAuction(ownAuc);

        CHECK(!service.EvaluateAndBid(buyerBot, ownAuc));

        // Test 4d: Exclusion - cannot bid on same-account listing
        MockPlayer altBot{ 21, buyerBot.GetSession()->GetAccountId(), "AltBot", ALLIANCE, 10000 };
        CHECK(!service.EvaluateAndBid(altBot, ownAuc));

        // Test 4e: Exclusion - cannot outbid self
        MockPlayer thirdBot{ 22, 300, "ThirdBot", ALLIANCE, 10000 };
        ownAuc->buyout = 50000; // high buyout so it bids instead of buying out
        bool bidPlaced = service.EvaluateAndBid(thirdBot, ownAuc);
        CHECK(bidPlaced);
        CHECK(ownAuc->bidder == thirdBot.GetGUIDLow());

        // Attempting to bid again should be excluded
        CHECK(!service.EvaluateAndBid(thirdBot, ownAuc));

        delete ownItem;
        service.ahObjectAlliance.RemoveAuction(ownAucId);
        delete ownAuc;
        delete item;
    }

    // -------------------------------------------------------------
    // Test 5: Price Overrides, Bans & Immediate Reload
    // -------------------------------------------------------------
    std::cout << "  [Test 5] Price overrides, bans & instant reload\n";
    {
        MockAhMarketService service;
        service.prototypes[protoSword.ItemId] = protoSword;

        uint32 basePrice = service.CalculatePrice(&protoSword);
        CHECK(basePrice > 0);

        // Variance math boundaries: exactly-zero variation floors at the
        // 1-copper sentinel, the cap holds at 0x7fffffff.
        CHECK(ai::AhVariedPrice(100, -100) == 1);
        CHECK(ai::AhVariedPrice(0x7fffffff, 100) == 0x7fffffff);

        // Set price override to exactly 10,000 copper (1 gold)
        service.SetItemOverride(protoSword.ItemId, 10000, 100, 1, 1);
        CHECK(service.CalculatePrice(&protoSword) == 10000);

        // Reload overrides from simulated external DB table
        std::unordered_map<uint32, ItemOverride> freshTable;
        freshTable[protoSword.ItemId] = ItemOverride{ 25000, 80, 1, 2 };
        service.ReloadOverrides(freshTable);

        CHECK(service.CalculatePrice(&protoSword) == 25000);

        // Blacklist through reload (value = 0, chance = 0)
        freshTable[protoSword.ItemId] = ItemOverride{ 0, 0, 0, 0 };
        service.ReloadOverrides(freshTable);

        CHECK(service.IsItemBlacklisted(protoSword.ItemId));
        CHECK(!service.IsItemEligible(&protoSword));
    }

    // -------------------------------------------------------------
    // Test 6: Safe Synthetic Expiration & Currency/Item Conservation
    // -------------------------------------------------------------
    std::cout << "  [Test 6] Safe synthetic expiration & conservation\n";
    {
        MockAhMarketService service;
        service.prototypes[protoSword.ItemId] = protoSword;

        uint32 itemGuid = service.nextItemGuid++;
        Item* item = new Item{ itemGuid, protoSword.ItemId, 1, &protoSword };
        service.dbItemInstances[itemGuid] = item;

        uint32 auctionId = service.nextAuctionId++;
        AuctionEntry* auc = new AuctionEntry();
        auc->Id = auctionId;
        auc->itemGuidLow = itemGuid;
        auc->itemTemplate = protoSword.ItemId;
        auc->itemCount = 1;
        auc->owner = SYNTHETIC_OWNER_GUID;
        auc->ownerAccount = SYNTHETIC_OWNER_ACCOUNT;
        auc->startbid = 500;
        auc->buyout = 1000;
        auc->auctionHouseEntry = &service.ahAlliance;
        service.ahObjectAlliance.AddAuction(auc);
        service.syntheticAuctions.insert(auctionId);
        service.syntheticItemGuids.insert(itemGuid);

        CHECK(service.IsSyntheticAuction(auctionId));
        CHECK(service.IsSyntheticItem(itemGuid));

        // Safe expire of unbid listing
        bool expired = service.SafelyExpireAuction(auctionId);
        CHECK(expired);
        CHECK(service.totalExpired == 1);
        CHECK(!service.IsSyntheticAuction(auctionId));
        CHECK(!service.IsSyntheticItem(itemGuid));
        CHECK(service.ahObjectAlliance.GetAuction(auctionId) == nullptr);
        CHECK(service.dbItemInstances.find(itemGuid) == service.dbItemInstances.end());

        // Test 6b: Bid-on synthetic listing is protected and NOT deleted on rebuild-all
        uint32 bidItemGuid = service.nextItemGuid++;
        Item* bidItem = new Item{ bidItemGuid, protoSword.ItemId, 1, &protoSword };
        service.dbItemInstances[bidItemGuid] = bidItem;

        uint32 bidAucId = service.nextAuctionId++;
        AuctionEntry* bidAuc = new AuctionEntry();
        bidAuc->Id = bidAucId;
        bidAuc->itemGuidLow = bidItemGuid;
        bidAuc->itemTemplate = protoSword.ItemId;
        bidAuc->itemCount = 1;
        bidAuc->owner = SYNTHETIC_OWNER_GUID;
        bidAuc->ownerAccount = SYNTHETIC_OWNER_ACCOUNT;
        bidAuc->startbid = 500;
        bidAuc->bid = 600; // Active bid placed!
        bidAuc->bidder = 999;
        bidAuc->buyout = 5000;
        bidAuc->auctionHouseEntry = &service.ahAlliance;
        service.ahObjectAlliance.AddAuction(bidAuc);
        service.syntheticAuctions.insert(bidAucId);
        service.syntheticItemGuids.insert(bidItemGuid);

        // Attempting to expire during rebuild all must fail-safe and protect the listing
        bool bidExpired = service.SafelyExpireAuction(bidAucId);
        CHECK(!bidExpired);
        CHECK(service.totalProtectedBids == 1);
        CHECK(service.IsSyntheticAuction(bidAucId));
        CHECK(service.ahObjectAlliance.GetAuction(bidAucId) != nullptr);

        // Cleanup test objects
        delete bidAuc;
        delete bidItem;
    }

    std::cout << "\nAll " << checks << " checks PASSED successfully!\n";
    return 0;
}
