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

#include "AhBotService.h"
#include "AhBotItemPool.h"
#include "AhBotPricing.h"
#include "AhBotSellerAccount.h"
#include "runtime/policy/ah/AhBotServicePolicy.h"

#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/RandomItemMgr.h"
#include "../host/ModuleLog.h"

#include "AuctionHouse/AuctionHouseMgr.h"
#include "Database/DBCStores.h"
#include "Database/DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "Mail/Mail.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_set>

namespace TortoiseBots
{

namespace
{

constexpr uint32_t kExpireSweepIntervalMs = 15000;
constexpr int64_t kEarlyExpireBufferSec = 60;
constexpr uint32_t kMailPurgeBatchSize = 50;

bool IsOfflineHardcore(uint32_t guidLow)
{
    if (PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(guidLow))
    {
        return data->uiHardcoreStatus == HARDCORE_MODE_STATUS_ALIVE ||
               data->uiHardcoreStatus == HARDCORE_MODE_STATUS_DEAD ||
               data->uiHardcoreStatus == HARDCORE_MODE_STATUS_HC60;
    }
    return false;
}

void SendBidderRefund(AuctionEntry* auction, MailAuctionAnswers reason)
{
    if (!auction || !auction->bidder)
        return;

    ObjectGuid bidderGuid(HIGHGUID_PLAYER, auction->bidder);
    Player* bidder = sObjectMgr.GetPlayer(bidderGuid);
    if (bidder && bidder->GetSession())
    {
        if (reason == AUCTION_OUTBIDDED)
            bidder->GetSession()->SendAuctionOutbiddedMail(auction);
        else
            bidder->GetSession()->SendAuctionCancelledToBidderMail(auction);
        return;
    }

    if (!sObjectMgr.GetPlayerAccountIdByGUID(bidderGuid))
        return;
    if (IsOfflineHardcore(auction->bidder))
        return;

    std::ostringstream subject;
    subject << auction->itemTemplate << ":0:" << reason;
    MailDraft(subject.str())
        .SetMoney(auction->bid)
        .SendMailTo(MailReceiver(nullptr, bidderGuid), auction, MAIL_CHECK_MASK_COPIED);
}

std::vector<AuctionHouseObject*> CollectDistinctHouses()
{
    std::set<AuctionHouseObject*> seen;
    std::vector<AuctionHouseObject*> houses;
    for (uint32_t i = 0; i < sAuctionHouseStore.GetNumRows(); ++i)
    {
        AuctionHouseEntry const* entry = sAuctionHouseStore.LookupEntry(i);
        if (!entry)
            continue;
        if (AuctionHouseObject* house = sAuctionMgr.GetAuctionsMap(entry))
        {
            if (seen.insert(house).second)
                houses.push_back(house);
        }
    }
    return houses;
}

bool RemoveAuctionFromHouse(AuctionHouseObject* house, AuctionEntry* auction, bool refundBidder)
{
    if (!house || !auction)
        return false;

    if (refundBidder && auction->bidder)
        SendBidderRefund(auction, AUCTION_CANCELLED_TO_BIDDER);

    if (Item* item = sAuctionMgr.GetAItem(auction->itemGuidLow))
    {
        item->DeleteFromDB();
        sAuctionMgr.RemoveAItem(auction->itemGuidLow);
        delete item;
    }
    else
    {
        CharacterDatabase.PExecute("DELETE FROM item_instance WHERE guid = '%u'", auction->itemGuidLow);
    }

    auction->DeleteFromDB();
    house->RemoveAuction(auction);
    delete auction;
    return true;
}

AuctionEntry* FindAuctionInHouses(uint32_t auctionId, AuctionHouseObject*& outHouse)
{
    for (AuctionHouseObject* house : CollectDistinctHouses())
    {
        if (AuctionEntry* auc = house->GetAuction(auctionId))
        {
            outHouse = house;
            return auc;
        }
    }
    outHouse = nullptr;
    return nullptr;
}

void HouseTargets(uint32_t houseId, uint32_t& outMin, uint32_t& outMax)
{
    switch (houseId)
    {
        case 1:
            outMin = sPlayerbotAIConfig.ahBotAllianceMinItems;
            outMax = sPlayerbotAIConfig.ahBotAllianceMaxItems;
            break;
        case 6:
            outMin = sPlayerbotAIConfig.ahBotHordeMinItems;
            outMax = sPlayerbotAIConfig.ahBotHordeMaxItems;
            break;
        case 7:
        default:
            outMin = sPlayerbotAIConfig.ahBotNeutralMinItems;
            outMax = sPlayerbotAIConfig.ahBotNeutralMaxItems;
            break;
    }
}

void ApplyRandomItemPropertiesOffline(Item* item, uint32_t itemId)
{
    // Offline-owned items have no Player* in world; write fields directly so
    // SetItemRandomProperties() does not touch an empty owner update queue.
    int32_t property = Item::GenerateItemRandomPropertyId(itemId);
    if (property <= 0)
        return;

    ItemRandomPropertiesEntry const* itemRand = sItemRandomPropertiesStore.LookupEntry(static_cast<uint32_t>(property));
    if (!itemRand)
        return;

    item->SetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID, itemRand->ID);
    for (uint32_t i = PROP_ENCHANTMENT_SLOT_0; i < PROP_ENCHANTMENT_SLOT_0 + 3; ++i)
    {
        item->SetUInt32Value(ITEM_FIELD_ENCHANTMENT + i * MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_ID_OFFSET,
                             itemRand->enchant_id[i - PROP_ENCHANTMENT_SLOT_0]);
        item->SetUInt32Value(ITEM_FIELD_ENCHANTMENT + i * MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_DURATION_OFFSET, 0);
        item->SetUInt32Value(ITEM_FIELD_ENCHANTMENT + i * MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_CHARGES_OFFSET, 0);
    }
}

} // namespace

AhBotService& AhBotService::Instance()
{
    static AhBotService instance;
    return instance;
}

std::vector<uint32_t> AhBotService::EnabledHouseIds() const
{
    if (sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
        return { 7 };
    return { 1, 6, 7 };
}

uint32_t AhBotService::RollIntervalMs(std::string const& raw) const
{
    ai::AhBotCycleRange range = ai::ParseAhBotRange(raw, 1, 1, 1440);
    uint32_t minutes = (range.min == range.max) ? range.min : urand(range.min, range.max);
    return minutes * 60000;
}

void AhBotService::LoadScheduleConfig()
{
    m_sellIntervalMs = RollIntervalMs(sPlayerbotAIConfig.ahBotMinutesBetweenSellCycle);
    m_buyIntervalMs = RollIntervalMs(sPlayerbotAIConfig.ahBotMinutesBetweenBuyCycle);
}

void AhBotService::EnsureVendorPricesLoaded()
{
    if (m_vendorPricesLoaded)
        return;
    m_vendorPricesLoaded = true;
    m_vendorSellPrices.clear();

    if (!sPlayerbotAIConfig.ahBotBuyerPreventOverpayingForVendorItems)
        return;

    auto res = WorldDatabase.Query("SELECT DISTINCT item FROM npc_vendor UNION SELECT DISTINCT item FROM npc_vendor_template");
    if (!res)
        return;

    do
    {
        uint32_t itemId = res->Fetch()[0].GetUInt32();
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (!proto || proto->Class == ITEM_CLASS_TRADE_GOODS || !proto->SellPrice)
            continue;
        m_vendorSellPrices[itemId] = proto->SellPrice;
    } while (res->NextRow());

    TB_LOG_BASIC("TortoiseBots: AhBot cached %zu non-trade vendor item sell prices", m_vendorSellPrices.size());
}

void AhBotService::EnsureInitialized()
{
    if (m_initialized)
        return;
    m_initialized = true;

    if (!m_poolLoaded)
    {
        AhBotItemPool::Instance().Load();
        m_poolLoaded = true;
    }
    EnsureVendorPricesLoaded();
    LoadScheduleConfig();
    EnqueueLegacyOwner0Cleanup();
}

void AhBotService::EnqueueLegacyOwner0Cleanup()
{
    if (m_legacyCleanupDone)
        return;

    m_pendingLegacyCleanupIds.clear();
    for (AuctionHouseObject* house : CollectDistinctHouses())
    {
        for (auto const& pair : *house->GetAuctions())
        {
            if (pair.second && ai::IsLegacySyntheticOwner(pair.second->owner))
                m_pendingLegacyCleanupIds.push_back(pair.second->Id);
        }
    }

    if (m_pendingLegacyCleanupIds.empty())
        m_legacyCleanupDone = true;
    else
        TB_LOG_BASIC("TortoiseBots: AhBot queued %zu legacy owner-0 auction(s) for cleanup",
            m_pendingLegacyCleanupIds.size());
}

void AhBotService::EnqueueOwnExpiringListings()
{
    int64_t now = static_cast<int64_t>(time(nullptr));
    auto const& sellerAccount = AhBotSellerAccount::Instance();

    for (AuctionHouseObject* house : CollectDistinctHouses())
    {
        for (auto const& pair : *house->GetAuctions())
        {
            AuctionEntry* auc = pair.second;
            if (!auc)
                continue;
            if (ai::ShouldRemoveOwnAhBotListing(sellerAccount.IsSellerGuid(auc->owner),
                    auc->bidder, static_cast<int64_t>(auc->expireTime), now, kEarlyExpireBufferSec))
            {
                m_pendingExpireIds.push_back(auc->Id);
            }
        }
    }
}

// A deleted seller's listings would otherwise stay up with no owner: the buyer
// would treat them as player listings and sale or expiry mail would go nowhere.
void AhBotService::RemoveRetiredSellerListings()
{
    std::vector<uint32_t> retired = AhBotSellerAccount::Instance().TakeRetiredSellerGuids();
    if (retired.empty())
        return;

    std::unordered_set<uint32_t> retiredSet(retired.begin(), retired.end());
    uint32_t removed = 0;
    for (AuctionHouseObject* house : CollectDistinctHouses())
    {
        std::vector<AuctionEntry*> doomed;
        for (auto const& pair : *house->GetAuctions())
            if (pair.second && retiredSet.count(pair.second->owner))
                doomed.push_back(pair.second);
        for (AuctionEntry* auc : doomed)
            if (RemoveAuctionFromHouse(house, auc, true))
                ++removed;
    }
    TB_LOG_BASIC("TortoiseBots: AhBot removed %u listing(s) of %zu deleted seller(s)", removed, retired.size());
}

void AhBotService::EnqueueSellCycle()
{
    if (!m_pendingSell.empty())
        return;
    if (!AhBotItemPool::Instance().IsLoaded() || AhBotItemPool::Instance().CandidateCount() == 0)
        return;

    for (uint32_t houseId : EnabledHouseIds())
    {
        AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(houseId);
        if (!ahEntry)
            continue;
        AuctionHouseObject* ahObject = sAuctionMgr.GetAuctionsMap(ahEntry);
        if (!ahObject)
            continue;

        uint32_t minItems = 0;
        uint32_t maxItems = 0;
        HouseTargets(houseId, minItems, maxItems);

        uint32_t toAdd = ai::ComputeAhBotSellCount(ahObject->GetCount(), minItems, maxItems,
            sPlayerbotAIConfig.ahBotItemsPerCycle);
        if (toAdd > 0)
            m_pendingSell.push_back({ houseId, toAdd });
    }
}

void AhBotService::EnqueueBuyCycle()
{
    if (!sPlayerbotAIConfig.ahBotBuyerEnabled || !m_pendingBuy.empty())
        return;

    ai::AhBotCycleRange candRange = ai::ParseAhBotRange(
        sPlayerbotAIConfig.ahBotBuyerBuyCandidatesPerBuyCycle, 1, 1, 1000);
    uint32_t candTarget = (candRange.min == candRange.max)
        ? candRange.min
        : urand(candRange.min, candRange.max);
    if (candTarget == 0)
        return;

    auto const& sellerAccount = AhBotSellerAccount::Instance();
    uint32_t sellerAccId = sellerAccount.AccountId();
    time_t now = time(nullptr);
    bool bidAgainstPlayers = sPlayerbotAIConfig.ahBotBuyerBidAgainstPlayers;

    for (uint32_t houseId : EnabledHouseIds())
    {
        AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(houseId);
        if (!ahEntry)
            continue;
        AuctionHouseObject* ahObject = sAuctionMgr.GetAuctionsMap(ahEntry);
        if (!ahObject || !ahObject->GetAuctions())
            continue;

        PendingBuyHouse pending;
        pending.houseId = houseId;
        pending.remaining = candTarget;

        for (auto const& pair : *ahObject->GetAuctions())
        {
            AuctionEntry* auc = pair.second;
            if (!auc || auc->expireTime <= now)
                continue;
            if (!auc->lockedIpAddress.empty() && auc->depositTime + 300 >= now)
                continue;
            if (auc->owner == 0 || sellerAccount.IsSellerGuid(auc->owner) ||
                (sellerAccId && auc->ownerAccount == sellerAccId))
                continue;
            if (auc->bidder && sellerAccount.IsSellerGuid(auc->bidder))
                continue;
            if (!bidAgainstPlayers && (auc->bid != 0 || auc->bidder != 0))
                continue;
            if (sRandomItemMgr.IsTransmogClone(auc->itemTemplate))
                continue;
            pending.candidateIds.push_back(auc->Id);
        }

        if (!pending.candidateIds.empty())
            m_pendingBuy.push_back(std::move(pending));
    }
}

bool AhBotService::StepLegacyCleanup()
{
    if (m_pendingLegacyCleanupIds.empty())
    {
        m_legacyCleanupDone = true;
        return false;
    }

    uint32_t auctionId = m_pendingLegacyCleanupIds.front();
    m_pendingLegacyCleanupIds.pop_front();

    AuctionHouseObject* house = nullptr;
    AuctionEntry* auc = FindAuctionInHouses(auctionId, house);
    if (auc && ai::IsLegacySyntheticOwner(auc->owner))
    {
        RemoveAuctionFromHouse(house, auc, true);
        ++m_totalLegacyCleaned;
    }

    if (m_pendingLegacyCleanupIds.empty())
    {
        m_legacyCleanupDone = true;
        TB_LOG_BASIC("TortoiseBots: AhBot finished legacy owner-0 cleanup (removed %llu)",
            static_cast<unsigned long long>(m_totalLegacyCleaned));
    }
    return true;
}

bool AhBotService::StepExpireRemoval()
{
    if (m_pendingExpireIds.empty())
        return false;

    uint32_t auctionId = m_pendingExpireIds.front();
    m_pendingExpireIds.pop_front();

    AuctionHouseObject* house = nullptr;
    AuctionEntry* auc = FindAuctionInHouses(auctionId, house);
    int64_t now = static_cast<int64_t>(time(nullptr));
    if (auc && ai::ShouldRemoveOwnAhBotListing(
            AhBotSellerAccount::Instance().IsSellerGuid(auc->owner),
            auc->bidder, static_cast<int64_t>(auc->expireTime), now, kEarlyExpireBufferSec))
    {
        RemoveAuctionFromHouse(house, auc, false);
        ++m_totalExpiredClean;
    }
    return true;
}

bool AhBotService::StepSellOne()
{
    while (!m_pendingSell.empty() && m_pendingSell.front().remaining == 0)
        m_pendingSell.pop_front();

    if (m_pendingSell.empty())
        return false;

    PendingSellHouse& job = m_pendingSell.front();
    uint32_t houseId = job.houseId;
    --job.remaining;
    if (job.remaining == 0)
        m_pendingSell.pop_front();

    AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(houseId);
    AuctionHouseObject* ahObject = ahEntry ? sAuctionMgr.GetAuctionsMap(ahEntry) : nullptr;
    if (!ahEntry || !ahObject)
        return true;

    uint32_t itemId = AhBotItemPool::Instance().PickItemForListing();
    if (!itemId)
    {
        m_pendingSell.clear();
        return false;
    }

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
        return true;

    uint32_t sellerGuid = AhBotSellerAccount::Instance().PickSellerGuid(houseId);
    uint32_t sellerAccId = AhBotSellerAccount::Instance().AccountId();
    if (!sellerGuid || !sellerAccId)
    {
        m_pendingSell.clear();
        return false;
    }

    uint32_t stackCount = AhBotPricing::Instance().StackSizeFor(proto);
    if (stackCount < 1)
        stackCount = 1;
    if (stackCount > proto->GetMaxStackSize())
        stackCount = proto->GetMaxStackSize();

    AhBotListingPrice price = AhBotPricing::Instance().PriceFor(proto, stackCount);
    uint32_t buyout = std::max<uint32_t>(1, price.buyout);
    uint32_t bid = std::max<uint32_t>(1, price.bid);
    if (bid > buyout)
        bid = buyout;

    uint32_t etimeMin = sPlayerbotAIConfig.ahBotListingExpireTimeInSecondsMin;
    uint32_t etimeMax = sPlayerbotAIConfig.ahBotListingExpireTimeInSecondsMax;
    uint32_t etime = (etimeMin >= etimeMax) ? etimeMin : urand(etimeMin, etimeMax);

    std::unique_ptr<Item> item(Item::CreateItem(itemId, stackCount));
    if (!item)
        return true;
    item->SetOwnerGuid(ObjectGuid(HIGHGUID_PLAYER, sellerGuid));

    ApplyRandomItemPropertiesOffline(item.get(), itemId);
    item->ClearUpdateMask(false);

    uint32_t deposit = AuctionHouseMgr::GetAuctionDeposit(ahEntry, etime, item.get());
    time_t now = time(nullptr);

    std::unique_ptr<AuctionEntry> auction(new AuctionEntry{});
    auction->Id = sObjectMgr.GenerateAuctionID();
    auction->itemGuidLow = item->GetGUIDLow();
    auction->itemTemplate = itemId;
    auction->owner = sellerGuid;
    auction->ownerAccount = sellerAccId;
    auction->startbid = bid;
    auction->bidder = 0;
    auction->bid = 0;
    auction->buyout = buyout;
    auction->depositTime = now;
    auction->expireTime = now + static_cast<time_t>(etime);
    auction->deposit = deposit;
    auction->auctionHouseEntry = ahEntry;

    CharacterDatabase.BeginTransaction();
    item->SaveToDB();
    auction->SaveToDB();
    CharacterDatabase.CommitTransaction();

    sAuctionMgr.AddAItem(item.release());
    ahObject->AddAuction(auction.release());
    ++m_totalListed;
    return true;
}

bool AhBotService::StepBuyOne()
{
    while (!m_pendingBuy.empty() &&
           (m_pendingBuy.front().remaining == 0 || m_pendingBuy.front().candidateIds.empty()))
    {
        m_pendingBuy.pop_front();
    }

    if (m_pendingBuy.empty())
        return false;

    PendingBuyHouse& job = m_pendingBuy.front();
    uint32_t houseId = job.houseId;
    size_t pickIdx = urand(0, static_cast<uint32_t>(job.candidateIds.size() - 1));
    uint32_t aucId = job.candidateIds[pickIdx];
    job.candidateIds[pickIdx] = job.candidateIds.back();
    job.candidateIds.pop_back();
    --job.remaining;
    if (job.remaining == 0 || job.candidateIds.empty())
        m_pendingBuy.pop_front();

    AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(houseId);
    AuctionHouseObject* ahObject = ahEntry ? sAuctionMgr.GetAuctionsMap(ahEntry) : nullptr;
    if (!ahObject)
        return true;

    AuctionEntry* auction = ahObject->GetAuction(aucId);
    if (!auction || auction->expireTime <= time(nullptr))
        return true;

    auto const& sellerAccount = AhBotSellerAccount::Instance();
    if (auction->owner == 0 || sellerAccount.IsSellerGuid(auction->owner))
        return true;
    if (auction->bidder && sellerAccount.IsSellerGuid(auction->bidder))
        return true;
    if (!sPlayerbotAIConfig.ahBotBuyerBidAgainstPlayers && (auction->bid != 0 || auction->bidder != 0))
        return true;

    Item* pItem = sAuctionMgr.GetAItem(auction->itemGuidLow);
    if (!pItem || pItem->GetCount() == 0)
        return true;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(auction->itemTemplate);
    if (!proto)
        return true;

    uint64_t fairPerUnit = AhBotPricing::Instance().BuyerFairValuePerUnit(proto);
    uint32_t vendorSellPrice = 0;
    if (sPlayerbotAIConfig.ahBotBuyerPreventOverpayingForVendorItems)
    {
        auto it = m_vendorSellPrices.find(proto->ItemId);
        if (it != m_vendorSellPrices.end())
            vendorSellPrice = it->second;
    }

    ai::AhBotBuyEval eval = ai::EvaluateAhBotBuyerCandidate(
        auction->buyout,
        auction->bid,
        auction->startbid,
        auction->GetAuctionOutBid(),
        fairPerUnit,
        pItem->GetCount(),
        vendorSellPrice,
        sPlayerbotAIConfig.ahBotBuyerAlwaysBidMaxCalculatedPrice,
        sPlayerbotAIConfig.ahBotBuyerPreventOverpayingForVendorItems);

    if (eval.action == ai::AhBotBuyDecision::Skip)
        return true;

    uint32_t buyerGuidLow = sellerAccount.PickSellerGuid(houseId);
    if (!buyerGuidLow || buyerGuidLow == auction->owner || buyerGuidLow == auction->bidder)
        return true;

    if (eval.action == ai::AhBotBuyDecision::Bid)
    {
        if (auction->bidder)
            SendBidderRefund(auction, AUCTION_OUTBIDDED);

        auction->bidder = buyerGuidLow;
        auction->bid = eval.amount;

        if (Player* ownerPlayer = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, auction->owner)))
        {
            if (ownerPlayer->GetSession())
                ownerPlayer->GetSession()->SendAuctionOwnerNotification(auction, false);
        }

        CharacterDatabase.PExecute("UPDATE auction SET buyguid = '%u', lastbid = '%u' WHERE id = '%u'",
            auction->bidder, auction->bid, auction->Id);
        ++m_totalBids;

        TB_LOG_DEBUG("TortoiseBots: AhBot buyer %u bid %u on auction %u (item %s x%u)",
            buyerGuidLow, auction->bid, auction->Id, proto->Name1.c_str(), pItem->GetCount());
    }
    else if (eval.action == ai::AhBotBuyDecision::Buyout)
    {
        if (auction->bidder && auction->bidder != buyerGuidLow)
            SendBidderRefund(auction, AUCTION_OUTBIDDED);

        auction->bidder = buyerGuidLow;
        auction->bid = auction->buyout;

        sAuctionMgr.SendAuctionSuccessfulMail(auction);
        sAuctionMgr.SendAuctionWonMail(auction);

        sAuctionMgr.RemoveAItem(auction->itemGuidLow);
        ahObject->RemoveAuction(auction);
        auction->DeleteFromDB();
        delete auction;
        ++m_totalBuyouts;

        TB_LOG_DEBUG("TortoiseBots: AhBot buyer %u bought out auction %u (item %s x%u) for %u",
            buyerGuidLow, aucId, proto->Name1.c_str(), pItem->GetCount(), eval.amount);
    }

    return true;
}

void AhBotService::RunBudgetedSlice()
{
    auto start = std::chrono::steady_clock::now();
    uint64_t budgetUs = sPlayerbotAIConfig.ahBotTickBudgetUs;
    uint32_t maxOps = sPlayerbotAIConfig.ahBotMaxListingsPerTick;
    uint32_t ops = 0;

    while (ops < maxOps)
    {
        bool didWork = false;
        if (!m_pendingLegacyCleanupIds.empty())
            didWork = StepLegacyCleanup();
        else if (!m_pendingExpireIds.empty())
            didWork = StepExpireRemoval();
        else if (!m_pendingSell.empty())
            didWork = StepSellOne();
        else if (!m_pendingBuy.empty())
            didWork = StepBuyOne();

        if (!didWork)
            break;

        ++ops;
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (static_cast<uint64_t>(elapsed) >= budgetUs)
            break;
    }

    auto totalElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    m_lastSliceUs = static_cast<uint64_t>(totalElapsed);
    m_maxSliceUs = std::max(m_maxSliceUs, m_lastSliceUs);
}

void AhBotService::Update(uint32_t diff)
{
    if (!sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.ahBotEnabled)
        return;

    AhBotSellerAccount::Instance().Update(diff);
    RemoveRetiredSellerListings();
    if (!AhBotSellerAccount::Instance().IsReady())
        return;

    EnsureInitialized();

    m_expireSweepElapsedMs += diff;
    if (m_expireSweepElapsedMs >= kExpireSweepIntervalMs && m_pendingExpireIds.empty())
    {
        m_expireSweepElapsedMs = 0;
        EnqueueOwnExpiringListings();
    }

    if (m_legacyCleanupDone)
    {
        bool purgedThisTick = false;

        m_sellElapsedMs += diff;
        if (m_sellElapsedMs >= m_sellIntervalMs)
        {
            m_sellElapsedMs = 0;
            m_sellIntervalMs = RollIntervalMs(sPlayerbotAIConfig.ahBotMinutesBetweenSellCycle);
            EnqueueSellCycle();
            AhBotSellerAccount::Instance().PurgeSellerMail(kMailPurgeBatchSize);
            purgedThisTick = true;
        }

        if (sPlayerbotAIConfig.ahBotBuyerEnabled)
        {
            m_buyElapsedMs += diff;
            if (m_buyElapsedMs >= m_buyIntervalMs)
            {
                m_buyElapsedMs = 0;
                m_buyIntervalMs = RollIntervalMs(sPlayerbotAIConfig.ahBotMinutesBetweenBuyCycle);
                EnqueueBuyCycle();
                if (!purgedThisTick)
                    AhBotSellerAccount::Instance().PurgeSellerMail(kMailPurgeBatchSize);
            }
        }
    }

    if (!m_pendingLegacyCleanupIds.empty() || !m_pendingExpireIds.empty() ||
        !m_pendingSell.empty() || !m_pendingBuy.empty())
    {
        RunBudgetedSlice();
    }
}

void AhBotService::Reload()
{
    sPlayerbotAIConfig.ReloadAhBotConfig();
    AhBotItemPool::Instance().Load();
    m_poolLoaded = true;
    AhBotPricing::Instance().Reload();

    m_vendorPricesLoaded = false;
    EnsureVendorPricesLoaded();
    LoadScheduleConfig();
    m_pendingSell.clear();
    m_pendingBuy.clear();

    TB_LOG_BASIC("TortoiseBots: AhBot reloaded configuration, item pool (%zu candidates), and pricing",
        AhBotItemPool::Instance().CandidateCount());
}

uint32_t AhBotService::Empty()
{
    auto const& sellerAccount = AhBotSellerAccount::Instance();
    uint32_t removed = 0;

    for (AuctionHouseObject* house : CollectDistinctHouses())
    {
        std::vector<AuctionEntry*> toRemove;
        for (auto const& pair : *house->GetAuctions())
        {
            AuctionEntry* auc = pair.second;
            if (!auc)
                continue;
            if (sellerAccount.IsSellerGuid(auc->owner) || ai::IsLegacySyntheticOwner(auc->owner))
                toRemove.push_back(auc);
        }

        for (AuctionEntry* auc : toRemove)
        {
            if (RemoveAuctionFromHouse(house, auc, true))
                ++removed;
        }
    }

    m_pendingLegacyCleanupIds.clear();
    m_pendingExpireIds.clear();
    m_pendingSell.clear();
    m_pendingBuy.clear();

    TB_LOG_BASIC("TortoiseBots: AhBot emptied %u listing(s) across all auction houses", removed);
    return removed;
}

void AhBotService::ForceUpdate()
{
    if (!sPlayerbotAIConfig.enabled || !sPlayerbotAIConfig.ahBotEnabled)
        return;

    AhBotSellerAccount::Instance().Update(0);
    if (!AhBotSellerAccount::Instance().IsReady())
        return;

    EnsureInitialized();

    m_sellElapsedMs = 0;
    m_buyElapsedMs = 0;
    EnqueueOwnExpiringListings();
    if (m_legacyCleanupDone)
    {
        EnqueueSellCycle();
        if (sPlayerbotAIConfig.ahBotBuyerEnabled)
            EnqueueBuyCycle();
        AhBotSellerAccount::Instance().PurgeSellerMail(kMailPurgeBatchSize);
    }

    RunBudgetedSlice();
}

std::string AhBotService::GetStatus() const
{
    std::ostringstream ss;
    ss << "AhBot Status: Enabled=" << (sPlayerbotAIConfig.ahBotEnabled ? "on" : "off")
       << " Buyer=" << (sPlayerbotAIConfig.ahBotBuyerEnabled ? "on" : "off")
       << " SellerAccount=[" << AhBotSellerAccount::Instance().GetStatus() << "]"
       << " ItemPool=[" << AhBotItemPool::Instance().GetStatus() << "]";

    for (uint32_t houseId : EnabledHouseIds())
    {
        AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(houseId);
        AuctionHouseObject* ahObject = ahEntry ? sAuctionMgr.GetAuctionsMap(ahEntry) : nullptr;
        uint32_t minItems = 0;
        uint32_t maxItems = 0;
        HouseTargets(houseId, minItems, maxItems);
        uint32_t cur = ahObject ? ahObject->GetCount() : 0;
        ss << " House" << houseId << "=" << cur << "/" << minItems << ".." << maxItems;
    }

    uint32_t pendingSellTotal = 0;
    for (PendingSellHouse const& s : m_pendingSell)
        pendingSellTotal += s.remaining;
    uint32_t pendingBuyTotal = 0;
    for (PendingBuyHouse const& b : m_pendingBuy)
        pendingBuyTotal += b.remaining;

    ss << " PendingSell=" << pendingSellTotal
       << " PendingBuy=" << pendingBuyTotal
       << " Listed=" << m_totalListed
       << " Bids=" << m_totalBids
       << " Buyouts=" << m_totalBuyouts
       << " EarlyExpired=" << m_totalExpiredClean
       << " LegacyCleaned=" << m_totalLegacyCleaned
       << " LastSliceUs=" << m_lastSliceUs
       << " MaxSliceUs=" << m_maxSliceUs;
    return ss.str();
}

} // namespace TortoiseBots
