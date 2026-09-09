// Standalone regression test for issue #87: Economy Native Auction Read Model and Personal Settlement.
// Self-contained mock harness verifying read model snapshotting, house/faction scoping,
// sub-copper unit price accuracy, trade lifecycle (post/bid/cancel/settle), hardcore rules,
// same-account exclusion, and exact item/currency conservation.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_auction_read_model.cpp -o /tmp/test_auction && /tmp/test_auction

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

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
// Faction enums matching core
constexpr uint32 ALLIANCE = 469;
constexpr uint32 HORDE = 67;
constexpr uint32 NEUTRAL_TEAM = 0;

constexpr uint32 MAIL_STATIONERY_DEFAULT = 1;
constexpr uint32 MAIL_STATIONERY_AUCTION = 62;
constexpr uint32 MAIL_NORMAL = 0;
constexpr uint32 MAIL_AUCTION = 2;

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
    uint32 BuyPrice;
    uint32 SellPrice;
    uint32 MaxStack;
};

struct Item
{
    uint32 guid;
    uint32 entry;
    uint32 count;
    ItemPrototype const* proto;

    uint32 GetCount() const { return count; }
    uint32 GetEntry() const { return entry; }
    ItemPrototype const* GetProto() const { return proto; }
};

struct AuctionEntry
{
    uint32 Id = 0;
    uint32 itemGuidLow = 0;
    uint32 itemTemplate = 0;
    uint32 owner = 0;
    uint32 ownerAccount = 0;
    uint32 startbid = 0;
    uint32 bid = 0;
    uint32 buyout = 0;
    uint32 bidder = 0;
    uint32 deposit = 0;
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

struct Mail
{
    uint32 messageID;
    uint32 sender;
    uint32 receiver;
    uint32 money = 0;
    std::vector<Item*> items;
    uint32 stationery = MAIL_STATIONERY_DEFAULT;
    uint32 messageType = MAIL_NORMAL;
    bool deleted = false;
};

// Mock World and Object Manager
struct MockCore
{
    bool twoSidedAuction = false;
    std::unordered_map<uint32, Item*> auctionItems; // itemGuidLow -> Item*
    std::unordered_map<uint32, std::vector<Mail>> mailboxes; // playerGuid -> mails
    uint32 nextAuctionId = 1;
    uint32 nextMailId = 1;
    uint32 totalBurntGold = 0; // Cut retained by the Auction House (sink)

    static uint32 GetAuctionHouseTeam(AuctionHouseEntry const* house)
    {
        switch (house->houseId)
        {
            case 1: case 2: case 3: return ALLIANCE;
            case 4: case 5: case 6: return HORDE;
            default: return NEUTRAL_TEAM;
        }
    }

    Item* GetAItem(uint32 guidLow)
    {
        auto it = auctionItems.find(guidLow);
        return it != auctionItems.end() ? it->second : nullptr;
    }

    void AddAItem(Item* it)
    {
        auctionItems[it->guid] = it;
    }

    void RemoveAItem(uint32 guidLow)
    {
        auctionItems.erase(guidLow);
    }

    void SendMail(uint32 receiverGuid, uint32 senderGuid, uint32 money, Item* item, uint32 stationery, uint32 messageType)
    {
        Mail m;
        m.messageID = nextMailId++;
        m.sender = senderGuid;
        m.receiver = receiverGuid;
        m.money = money;
        if (item) m.items.push_back(item);
        m.stationery = stationery;
        m.messageType = messageType;
        mailboxes[receiverGuid].push_back(m);
    }
};

static MockCore sMockCore;

struct MockPlayer
{
    uint32 guid;
    uint32 accountId;
    std::string name;
    uint32 team; // ALLIANCE or HORDE
    uint32 money = 0;
    bool isHardcore = false;
    std::vector<Item*> inventory;

    uint32 GetGUIDLow() const { return guid; }
    uint32 GetTeam() const { return team; }
    uint32 GetMoney() const { return money; }
    bool IsHardcore() const { return isHardcore; }

    void ModifyMoney(int32 delta)
    {
        if (delta < 0)
        {
            uint32 absDelta = uint32(-delta);
            CHECK(money >= absDelta);
            money -= absDelta;
        }
        else
        {
            money += uint32(delta);
        }
    }

    Item* GetItemByGuid(uint32 itemGuid)
    {
        for (auto* it : inventory)
            if (it->guid == itemGuid)
                return it;
        return nullptr;
    }

    void RemoveItem(uint32 itemGuid)
    {
        auto it = std::remove_if(inventory.begin(), inventory.end(), [itemGuid](Item* i) {
            return i->guid == itemGuid;
        });
        inventory.erase(it, inventory.end());
    }
};

struct MockAuctionHouseObject
{
    std::map<uint32, AuctionEntry*> AuctionsMap;

    void AddAuction(AuctionEntry* e)
    {
        AuctionsMap[e->Id] = e;
    }

    AuctionEntry* GetAuction(uint32 id)
    {
        auto it = AuctionsMap.find(id);
        return it != AuctionsMap.end() ? it->second : nullptr;
    }

    bool RemoveAuction(AuctionEntry* e)
    {
        return AuctionsMap.erase(e->Id) > 0;
    }
};

// Standalone implementation of Read Model and Appraisal Logic matching TortoiseBots
struct MockRandomBotFacade
{
    std::unordered_map<uint32, std::vector<AuctionEntry>> ahMirror;

    void LoadAuctionPrices(const std::vector<MockAuctionHouseObject*>& houses)
    {
        ahMirror.clear();
        std::vector<MockAuctionHouseObject*> visited;
        for (auto* house : houses)
        {
            if (!house) continue;
            if (std::find(visited.begin(), visited.end(), house) != visited.end())
                continue;
            visited.push_back(house);

            for (auto const& pair : house->AuctionsMap)
            {
                AuctionEntry const* entry = pair.second;
                if (!entry || !entry->buyout)
                    continue;

                Item const* item = sMockCore.GetAItem(entry->itemGuidLow);
                if (!item || !item->GetCount())
                    continue;

                constexpr size_t kMaxAuctionsPerItem = 64;
                auto& listings = ahMirror[entry->itemTemplate];
                if (listings.size() < kMaxAuctionsPerItem)
                {
                    listings.push_back(*entry);
                }
                else
                {
                    float currentUnitPrice = float(entry->buyout) / float(item->GetCount());
                    size_t maxIdx = 0;
                    float maxUnitPrice = 0.0f;
                    for (size_t idx = 0; idx < listings.size(); ++idx)
                    {
                        Item const* existingItem = sMockCore.GetAItem(listings[idx].itemGuidLow);
                        uint32 existingCount = existingItem ? existingItem->GetCount() : 1;
                        float existingUnitPrice = float(listings[idx].buyout) / float(existingCount);
                        if (existingUnitPrice > maxUnitPrice)
                        {
                            maxUnitPrice = existingUnitPrice;
                            maxIdx = idx;
                        }
                    }
                    if (currentUnitPrice < maxUnitPrice)
                    {
                        listings[maxIdx] = *entry;
                    }
                }
            }
        }
    }

    const std::vector<AuctionEntry>& GetAhPrices(uint32 itemId) const
    {
        static const std::vector<AuctionEntry> empty;
        auto it = ahMirror.find(itemId);
        return it == ahMirror.end() ? empty : it->second;
    }

    std::vector<AuctionEntry> GetAhPrices(uint32 itemId, uint32 houseFaction) const
    {
        std::vector<AuctionEntry> result;
        auto it = ahMirror.find(itemId);
        if (it == ahMirror.end())
            return result;

        for (auto const& entry : it->second)
        {
            if (sMockCore.twoSidedAuction)
            {
                result.push_back(entry);
                continue;
            }

            uint32 team = entry.auctionHouseEntry ? MockCore::GetAuctionHouseTeam(entry.auctionHouseEntry) : NEUTRAL_TEAM;
            if (team == NEUTRAL_TEAM || team == houseFaction || houseFaction == NEUTRAL_TEAM)
            {
                result.push_back(entry);
            }
        }
        return result;
    }

    std::vector<AuctionEntry> GetAhPrices(uint32 itemId, MockPlayer const* bot) const
    {
        if (!bot) return GetAhPrices(itemId, (uint32)0);
        return GetAhPrices(itemId, bot->GetTeam());
    }
};

static MockRandomBotFacade sFacade;

// Appraisal functions
uint32 GetAuctionItemCount(AuctionEntry const& auction)
{
    Item* item = sMockCore.GetAItem(auction.itemGuidLow);
    return item ? item->GetCount() : 0;
}

uint32 GetAHMedianBuyoutPricePerItem(ItemPrototype const* proto, MockPlayer const* bot = nullptr)
{
    std::vector<float> prices;
    std::vector<AuctionEntry> listings = sFacade.GetAhPrices(proto->ItemId, bot);
    for (auto& auction : listings)
    {
        uint32 itemCount = GetAuctionItemCount(auction);
        if (itemCount)
            prices.push_back((float)auction.buyout / (float)itemCount);
    }
    if (prices.empty()) return 0;

    size_t n = prices.size() / 2;
    std::nth_element(prices.begin(), prices.begin() + n, prices.end());
    float median = prices[n];
    if (median > 0 && median < 1)
        return 1;
    return static_cast<uint32>(median);
}

uint32 GetAHListingLowestBuyoutPricePerItem(ItemPrototype const* proto, MockPlayer const* bot = nullptr)
{
    float minPrice = 0;
    bool found = false;
    std::vector<AuctionEntry> listings = sFacade.GetAhPrices(proto->ItemId, bot);
    for (auto& auction : listings)
    {
        uint32 itemCount = GetAuctionItemCount(auction);
        if (itemCount)
        {
            float unitPrice = (float)auction.buyout / (float)itemCount;
            if (!found || unitPrice < minPrice)
            {
                minPrice = unitPrice;
                found = true;
            }
        }
    }
    if (!found) return 0;
    if (minPrice > 0 && minPrice < 1) return 1;
    return (uint32)minPrice;
}

uint32 DesiredPricePerItem(MockPlayer const* bot, ItemPrototype const* proto, uint32 count, uint32 priceModifier)
{
    AuctionEntry lowestPrice;
    lowestPrice.Id = 0;
    uint32 lowestItemCount = 0;

    std::vector<AuctionEntry> listings = sFacade.GetAhPrices(proto->ItemId, bot);
    for (auto& auction : listings)
    {
        uint32 itemCount = GetAuctionItemCount(auction);
        if (itemCount != count) continue;

        float pricePerItem = float(auction.buyout) / float(itemCount);
        if (lowestPrice.Id == 0 || pricePerItem < float(lowestPrice.buyout) / float(lowestItemCount))
        {
            lowestPrice = auction;
            lowestItemCount = itemCount;
        }
    }

    uint32 lowestBuyoutItemPricePerItem = lowestItemCount ?
        static_cast<uint32>(float(lowestPrice.buyout) / float(lowestItemCount)) : 0;

    uint32 minAhPrice = lowestBuyoutItemPricePerItem;
    uint32 maxAhPrice = GetAHMedianBuyoutPricePerItem(proto, bot) * 1.5f;
    if (!maxAhPrice) maxAhPrice = minAhPrice ? minAhPrice * 1.5f : proto->SellPrice * 2;
    if (!minAhPrice) minAhPrice = proto->SellPrice ? proto->SellPrice : 1;

    uint32 desiredPrice = minAhPrice + static_cast<uint32>((maxAhPrice - minAhPrice) * priceModifier / 100);

    if (lowestBuyoutItemPricePerItem > 0 && lowestPrice.owner != (bot ? bot->GetGUIDLow() : 0))
    {
        uint32 undercut = std::max<uint32>(1, uint32(lowestBuyoutItemPricePerItem * 0.05f));
        if (undercut < lowestBuyoutItemPricePerItem)
            desiredPrice = lowestBuyoutItemPricePerItem - undercut;
        else
            desiredPrice = lowestBuyoutItemPricePerItem - 1;
    }

    desiredPrice = std::max(minAhPrice, desiredPrice);
    if (!desiredPrice) desiredPrice = 1;
    return desiredPrice;
}

// Canonical trade operations mimicking core session handlers
bool CoreHandleAuctionSellItem(MockPlayer& seller, Item* item, uint32 bid, uint32 buyout, AuctionHouseEntry const* ahEntry, MockAuctionHouseObject& ahObj)
{
    if (!item || !ahEntry) return false;
    uint32 deposit = (item->GetProto()->SellPrice * item->GetCount() * 8) * ahEntry->depositPercent / 100;
    if (deposit < 10) deposit = 10;
    if (seller.GetMoney() < deposit) return false;

    seller.ModifyMoney(-int32(deposit));
    seller.RemoveItem(item->guid);

    AuctionEntry* ah = new AuctionEntry;
    ah->Id = sMockCore.nextAuctionId++;
    ah->itemGuidLow = item->guid;
    ah->itemTemplate = item->entry;
    ah->owner = seller.GetGUIDLow();
    ah->ownerAccount = seller.accountId;
    ah->startbid = bid;
    ah->bid = 0;
    ah->bidder = 0;
    ah->buyout = buyout;
    ah->deposit = deposit;
    ah->auctionHouseEntry = ahEntry;

    ahObj.AddAuction(ah);
    sMockCore.AddAItem(item);
    return true;
}

bool CoreHandleAuctionPlaceBid(MockPlayer& bidder, uint32 auctionId, uint32 price, MockAuctionHouseObject& ahObj, MockPlayer* currentBidderPlayer, MockPlayer* sellerPlayer)
{
    AuctionEntry* auction = ahObj.GetAuction(auctionId);
    if (!auction) return false;

    // Same-account and owner protection
    if (auction->owner == bidder.GetGUIDLow() || auction->ownerAccount == bidder.accountId)
        return false;

    if (price < auction->startbid || price <= auction->bid)
        return false;

    if (price > bidder.GetMoney())
        return false;

    if (price < auction->buyout || auction->buyout == 0) // Normal Bid
    {
        bidder.ModifyMoney(-int32(price));
        if (auction->bidder) // Refund old bidder
        {
            if (currentBidderPlayer && !currentBidderPlayer->IsHardcore())
            {
                sMockCore.SendMail(auction->bidder, 0, auction->bid, nullptr, MAIL_STATIONERY_AUCTION, MAIL_AUCTION);
            }
        }
        auction->bidder = bidder.GetGUIDLow();
        auction->bid = price;
        return true;
    }
    else // Buyout
    {
        bidder.ModifyMoney(-int32(auction->buyout));
        if (auction->bidder) // Refund old bidder
        {
            if (currentBidderPlayer && !currentBidderPlayer->IsHardcore())
            {
                sMockCore.SendMail(auction->bidder, 0, auction->bid, nullptr, MAIL_STATIONERY_AUCTION, MAIL_AUCTION);
            }
        }
        auction->bidder = bidder.GetGUIDLow();
        auction->bid = auction->buyout;

        uint32 cut = auction->GetAuctionCut();
        sMockCore.totalBurntGold += cut;
        uint32 profit = auction->bid + auction->deposit - cut;

        // Seller payout mail
        if (sellerPlayer && !sellerPlayer->IsHardcore())
        {
            sMockCore.SendMail(auction->owner, auction->bidder, profit, nullptr, MAIL_STATIONERY_AUCTION, MAIL_AUCTION);
        }

        // Winner item mail
        Item* item = sMockCore.GetAItem(auction->itemGuidLow);
        CHECK(item != nullptr);
        sMockCore.RemoveAItem(auction->itemGuidLow);

        if (!bidder.IsHardcore())
        {
            sMockCore.SendMail(bidder.GetGUIDLow(), auction->owner, 0, item, MAIL_STATIONERY_AUCTION, MAIL_AUCTION);
        }
        else
        {
            delete item;
        }

        ahObj.RemoveAuction(auction);
        delete auction;
        return true;
    }
}

bool CoreHandleAuctionRemoveItem(MockPlayer& seller, uint32 auctionId, MockAuctionHouseObject& ahObj, MockPlayer* bidderPlayer)
{
    AuctionEntry* auction = ahObj.GetAuction(auctionId);
    if (!auction || auction->owner != seller.GetGUIDLow())
        return false;

    Item* item = sMockCore.GetAItem(auction->itemGuidLow);
    if (!item) return false;

    if (auction->bidder > 0)
    {
        uint32 cut = auction->GetAuctionCut();
        if (seller.GetMoney() < cut) return false;
        seller.ModifyMoney(-int32(cut));
        sMockCore.totalBurntGold += cut;

        // Refund bidder
        if (bidderPlayer && !bidderPlayer->IsHardcore())
        {
            sMockCore.SendMail(auction->bidder, 0, auction->bid, nullptr, MAIL_STATIONERY_AUCTION, MAIL_AUCTION);
        }
    }

    // Return item to seller
    sMockCore.RemoveAItem(auction->itemGuidLow);
    sMockCore.SendMail(seller.GetGUIDLow(), 0, 0, item, MAIL_STATIONERY_AUCTION, MAIL_AUCTION);

    ahObj.RemoveAuction(auction);
    delete auction;
    return true;
}

void BotSettleMailbox(MockPlayer& bot)
{
    auto it = sMockCore.mailboxes.find(bot.GetGUIDLow());
    if (it == sMockCore.mailboxes.end()) return;

    for (auto& mail : it->second)
    {
        if (mail.deleted) continue;

        if (mail.money > 0)
        {
            bot.ModifyMoney(mail.money);
            mail.money = 0;
        }

        if (!mail.items.empty())
        {
            for (auto* item : mail.items)
                bot.inventory.push_back(item);
            mail.items.clear();
        }

        mail.deleted = true;
    }
}

int main()
{
    std::cout << "Starting Test Suite: Native Auction Read Model & Personal Settlement...\n";

    // Setup DBC entries
    AuctionHouseEntry ahAlliance{ 1, 11, 5 }; // Alliance 5% cut
    AuctionHouseEntry ahHorde{ 4, 85, 5 };    // Horde 5% cut
    AuctionHouseEntry ahNeutral{ 7, 0, 15 };  // Neutral 15% cut

    ItemPrototype protoLinen{ 2589, "Linen Cloth", 10, 2, 20 };
    ItemPrototype protoSilk{ 4306, "Silk Cloth", 60, 12, 20 };

    MockAuctionHouseObject houseAlliance;
    MockAuctionHouseObject houseHorde;
    MockAuctionHouseObject houseNeutral;
    std::vector<MockAuctionHouseObject*> allHouses = { &houseAlliance, &houseHorde, &houseNeutral };

    // -------------------------------------------------------------
    // 1. Read Model Snapshot, Faction Scoping, and Unit-Price Accuracy
    // -------------------------------------------------------------
    {
        // Initially empty
        sFacade.LoadAuctionPrices(allHouses);
        CHECK(sFacade.GetAhPrices(protoLinen.ItemId).empty());
        CHECK(GetAHMedianBuyoutPricePerItem(&protoLinen) == 0);
        CHECK(GetAHListingLowestBuyoutPricePerItem(&protoLinen) == 0);

        // Add an Alliance auction for Linen (stack of 5 for 50 copper -> unit price 10 copper)
        Item* item1 = new Item{ 101, protoLinen.ItemId, 5, &protoLinen };
        sMockCore.AddAItem(item1);
        AuctionEntry* auc1 = new AuctionEntry{ 1, item1->guid, protoLinen.ItemId, 10, 1, 30, 0, 50, 0, 5, &ahAlliance };
        houseAlliance.AddAuction(auc1);

        // Add a Horde auction for Linen (stack of 2 for 40 copper -> unit price 20 copper)
        Item* item2 = new Item{ 102, protoLinen.ItemId, 2, &protoLinen };
        sMockCore.AddAItem(item2);
        AuctionEntry* auc2 = new AuctionEntry{ 2, item2->guid, protoLinen.ItemId, 20, 2, 20, 0, 40, 0, 5, &ahHorde };
        houseHorde.AddAuction(auc2);

        // Add a Neutral auction for Linen (sub-copper: stack of 10 for 5 copper -> unit price 0.5 copper)
        Item* item3 = new Item{ 103, protoLinen.ItemId, 10, &protoLinen };
        sMockCore.AddAItem(item3);
        AuctionEntry* auc3 = new AuctionEntry{ 3, item3->guid, protoLinen.ItemId, 30, 3, 3, 0, 5, 0, 5, &ahNeutral };
        houseNeutral.AddAuction(auc3);

        sFacade.LoadAuctionPrices(allHouses);

        MockPlayer allyBot{ 11, 101, "AllyBot", ALLIANCE, 1000, false, {} };
        MockPlayer hordeBot{ 21, 102, "HordeBot", HORDE, 1000, false, {} };

        // Test Faction Scoping (two-sided = false):
        sMockCore.twoSidedAuction = false;
        auto allyPrices = sFacade.GetAhPrices(protoLinen.ItemId, &allyBot);
        // Ally sees Alliance (auc1) + Neutral (auc3) = 2 listings
        CHECK(allyPrices.size() == 2);

        auto hordePrices = sFacade.GetAhPrices(protoLinen.ItemId, &hordeBot);
        // Horde sees Horde (auc2) + Neutral (auc3) = 2 listings
        CHECK(hordePrices.size() == 2);

        // With two-sided = true, both see all 3 listings
        sMockCore.twoSidedAuction = true;
        CHECK(sFacade.GetAhPrices(protoLinen.ItemId, &allyBot).size() == 3);
        CHECK(sFacade.GetAhPrices(protoLinen.ItemId, &hordeBot).size() == 3);
        sMockCore.twoSidedAuction = false;

        // Sub-copper accuracy test:
        // Neutral listing has unit price 0.5 copper.
        // Lowest buyout must preserve positive sentinel 1 (never 0 of "no listing")
        uint32 lowest = GetAHListingLowestBuyoutPricePerItem(&protoLinen, &allyBot);
        CHECK(lowest == 1); // 0.5 rounded to sentinel 1

        // Median price test for Alliance:
        // Listings: 0.5 copper and 10 copper. Median of 2 elements is index 1 -> 10 copper.
        uint32 median = GetAHMedianBuyoutPricePerItem(&protoLinen, &allyBot);
        CHECK(median == 10);

        // Desired price undercutting test:
        uint32 desired = DesiredPricePerItem(&allyBot, &protoLinen, 5, 50);
        CHECK(desired > 0);

        // Clean up test auctions
        houseAlliance.RemoveAuction(auc1); delete auc1; sMockCore.RemoveAItem(item1->guid); delete item1;
        houseHorde.RemoveAuction(auc2); delete auc2; sMockCore.RemoveAItem(item2->guid); delete item2;
        houseNeutral.RemoveAuction(auc3); delete auc3; sMockCore.RemoveAItem(item3->guid); delete item3;
    }

    // -------------------------------------------------------------
    // 2. Personal Bot Trade Lifecycle: Post -> Outbid -> Buyout -> Settlement
    //    Verifies exact currency and item conservation (no duplicated gold/items)
    // -------------------------------------------------------------
    {
        MockPlayer seller{ 1, 10, "SellerBot", ALLIANCE, 10000, false, {} };
        MockPlayer bidder1{ 2, 20, "BidderOne", ALLIANCE, 10000, false, {} };
        MockPlayer buyer{ 3, 30, "BuyerBot", ALLIANCE, 10000, false, {} };

        uint32 initialSystemMoney = seller.GetMoney() + bidder1.GetMoney() + buyer.GetMoney();
        sMockCore.totalBurntGold = 0;

        Item* saleItem = new Item{ 501, protoSilk.ItemId, 5, &protoSilk };
        seller.inventory.push_back(saleItem);
        CHECK(seller.inventory.size() == 1);

        // A. Post Item
        uint32 startBid = 200;
        uint32 buyout = 500;
        bool postOk = CoreHandleAuctionSellItem(seller, saleItem, startBid, buyout, &ahAlliance, houseAlliance);
        CHECK(postOk);
        CHECK(seller.inventory.empty()); // Item transferred to AH
        CHECK(seller.GetMoney() < 10000); // Deposit deducted
        uint32 depositPaid = 10000 - seller.GetMoney();
        CHECK(depositPaid > 0);

        AuctionEntry* posted = houseAlliance.GetAuction(1);
        CHECK(posted != nullptr);
        CHECK(posted->deposit == depositPaid);

        // B. Same-account Exclusion Check:
        // Another character on the same account (accountId 10) cannot bid
        MockPlayer sameAccountAlt{ 4, 10, "SellerAlt", ALLIANCE, 10000, false, {} };
        bool sameAccBid = CoreHandleAuctionPlaceBid(sameAccountAlt, posted->Id, 250, houseAlliance, nullptr, &seller);
        CHECK(!sameAccBid); // Rejected!

        // C. Bidder 1 places a bid of 250 copper
        bool bid1Ok = CoreHandleAuctionPlaceBid(bidder1, posted->Id, 250, houseAlliance, nullptr, &seller);
        CHECK(bid1Ok);
        CHECK(bidder1.GetMoney() == 10000 - 250);
        CHECK(posted->bid == 250);
        CHECK(posted->bidder == bidder1.GetGUIDLow());

        // D. Buyer buys out the auction (500 copper)
        // This should:
        // 1. Deduct 500 from buyer.
        // 2. Outbid/refund bidder1 (250 copper via mailbox).
        // 3. Send successful mail with profit to seller: profit = buyout (500) + deposit - cut (5% of 500 = 25)
        // 4. Send won mail with item to buyer.
        bool buyoutOk = CoreHandleAuctionPlaceBid(buyer, posted->Id, 500, houseAlliance, &bidder1, &seller);
        CHECK(buyoutOk);
        CHECK(buyer.GetMoney() == 10000 - 500);
        CHECK(houseAlliance.GetAuction(1) == nullptr); // Auction cleaned up

        // E. Mail Settlement
        // Settle Bidder 1 mailbox:
        BotSettleMailbox(bidder1);
        CHECK(bidder1.GetMoney() == 10000); // Exact 250 copper refund received, 0 leakage!

        // Settle Seller mailbox:
        BotSettleMailbox(seller);
        // Seller gets profit = 500 + depositPaid - 25.
        // Net seller money = (10000 - depositPaid) + (500 + depositPaid - 25) = 10000 + 475.
        CHECK(seller.GetMoney() == 10475);

        // Settle Buyer mailbox:
        BotSettleMailbox(buyer);
        CHECK(buyer.inventory.size() == 1);
        CHECK(buyer.inventory[0]->guid == 501); // Item received!

        // F. Global Economy Conservation Audit:
        // Total money after = seller + bidder1 + buyer + AH cut
        uint32 finalSystemMoney = seller.GetMoney() + bidder1.GetMoney() + buyer.GetMoney() + sMockCore.totalBurntGold;
        CHECK(finalSystemMoney == initialSystemMoney);
        CHECK(sMockCore.totalBurntGold == 25); // Exact 5% cut burnt as economy gold sink
        CHECK(buyer.inventory.size() + seller.inventory.size() == 1); // Exactly 1 item conserved

        // Clean up
        delete buyer.inventory[0];
    }

    // -------------------------------------------------------------
    // 3. Cancellation Lifecycle and Cut Handling
    // -------------------------------------------------------------
    {
        MockPlayer seller{ 5, 50, "CancellerBot", ALLIANCE, 10000, false, {} };
        MockPlayer bidder{ 6, 60, "BidderTwo", ALLIANCE, 10000, false, {} };

        Item* cancelItem = new Item{ 601, protoLinen.ItemId, 10, &protoLinen };
        seller.inventory.push_back(cancelItem);

        // Post
        CHECK(CoreHandleAuctionSellItem(seller, cancelItem, 100, 300, &ahAlliance, houseAlliance));
        AuctionEntry* auc = houseAlliance.GetAuction(2);
        CHECK(auc != nullptr);

        // Bidder places 150 bid
        CHECK(CoreHandleAuctionPlaceBid(bidder, auc->Id, 150, houseAlliance, nullptr, &seller));
        CHECK(bidder.GetMoney() == 10000 - 150);

        // Seller cancels auction with active bidder:
        // Core charges seller the auction cut (5% of 150 = 7 copper), refunds bidder, returns item to seller.
        uint32 sellerMoneyBeforeCancel = seller.GetMoney();
        CHECK(CoreHandleAuctionRemoveItem(seller, auc->Id, houseAlliance, &bidder));
        CHECK(seller.GetMoney() == sellerMoneyBeforeCancel - 7); // Cut deducted

        // Settle mailboxes
        BotSettleMailbox(bidder);
        CHECK(bidder.GetMoney() == 10000); // Bidder fully refunded

        BotSettleMailbox(seller);
        CHECK(seller.inventory.size() == 1);
        CHECK(seller.inventory[0]->guid == 601); // Item returned to seller

        delete seller.inventory[0];
    }

    // -------------------------------------------------------------
    // 4. Hardcore Settlement Safety
    // -------------------------------------------------------------
    {
        MockPlayer hcBidder{ 7, 70, "HcBidder", ALLIANCE, 5000, true /* hardcore */, {} };
        MockPlayer regularSeller{ 8, 80, "RegSeller", ALLIANCE, 5000, false, {} };
        MockPlayer regularOutbidder{ 9, 90, "RegOutbidder", ALLIANCE, 5000, false, {} };

        Item* item = new Item{ 701, protoLinen.ItemId, 1, &protoLinen };
        regularSeller.inventory.push_back(item);
        CHECK(CoreHandleAuctionSellItem(regularSeller, item, 50, 200, &ahAlliance, houseAlliance));
        AuctionEntry* auc = houseAlliance.GetAuction(3);
        CHECK(auc != nullptr);

        // Hardcore player bids 60
        CHECK(CoreHandleAuctionPlaceBid(hcBidder, auc->Id, 60, houseAlliance, nullptr, &regularSeller));
        CHECK(hcBidder.GetMoney() == 5000 - 60);

        // Regular player outbids hardcore player (80)
        // Hardcore rules: No refund mail sent to hardcore characters (prevents hardcore mail death-transfer exploit)
        CHECK(CoreHandleAuctionPlaceBid(regularOutbidder, auc->Id, 80, houseAlliance, &hcBidder, &regularSeller));

        // Verify no mail was queued for hcBidder
        CHECK(sMockCore.mailboxes[hcBidder.GetGUIDLow()].empty());

        // Cancel auction
        CHECK(CoreHandleAuctionRemoveItem(regularSeller, auc->Id, houseAlliance, &regularOutbidder));
        BotSettleMailbox(regularOutbidder);
        BotSettleMailbox(regularSeller);
        delete regularSeller.inventory[0];
    }

    std::cout << "All " << checks << " checks PASSED!\n";
    return 0;
}
