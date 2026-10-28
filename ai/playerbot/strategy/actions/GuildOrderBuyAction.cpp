
#include "playerbot/playerbot.h"
#include "GuildOrderBuyAction.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "runtime/GuildOrderBuyPolicy.h"
#include "runtime/GuildOrderStore.h"
#include "AuctionHouse/AuctionHouseMgr.h"

#include <mutex>

using namespace ai;
using TortoiseBots::GuildOrders::BuyOwnedCount;
using TortoiseBots::GuildOrders::BuyStillMissing;
using TortoiseBots::GuildOrders::BuyoutBeatsPerItem;

bool GuildOrderBuyAction::isUseful()
{
    if (!bot->GetGuildId())
        return false;

    if (bot->IsInCombat())
        return false;

    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (order.type != GuildOrderType::AuctionHouse || order.target.empty())
        return false;

    if (!FindNearbyAuctioneer())
        return false;

    if (AI_VALUE(uint8, "bag space") > 90)
        return false;

    return OrderStillNeedsWork(bot, order);
}

Unit* GuildOrderBuyAction::FindNearbyAuctioneer()
{
    std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
    for (auto& guid : npcs)
    {
        Unit* npc = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_AUCTIONEER);
        if (npc)
            return npc;
    }
    return nullptr;
}

bool GuildOrderBuyAction::Execute(Event& event)
{
    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (order.type != GuildOrderType::AuctionHouse || !order.itemId || !order.amount)
        return false;
    if (!TortoiseBots::GuildOrders::Find(bot->GetGUIDLow()))
        return false;

    Unit* auctioneer = FindNearbyAuctioneer();
    if (!auctioneer)
        return false;

    std::optional<TortoiseBots::GuildOrders::OrderRecord> stored =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    uint32 delivered = (stored && stored->itemId == order.itemId) ? stored->delivered : 0;
    uint32 owned = BuyOwnedCount(delivered, ai->GetInventoryItemsCountWithId(order.itemId),
        OrderMailboxItems(bot, order.itemId));
    uint32 missing = BuyStillMissing(order.amount, owned);
    if (!missing)
        return false;

    std::unique_lock<std::mutex> ahLock(sRandomBotFacade.m_ahActionMutex, std::try_to_lock);
    if (!ahLock.owns_lock())
        return false;

    bool bought = false;
    bool visitDone = false;

    AuctionHouseEntry const* ahEntry = AuctionHouseMgr::GetAuctionHouseEntry(auctioneer);
    AuctionHouseObject* auctionHouse = ahEntry ? sAuctionMgr.GetAuctionsMap(ahEntry) : nullptr;
    if (auctionHouse)
    {
        // Real gold only: the free-money budget counts cheat gold the core
        // would refuse, and the bid would fail every tick instead of falling
        // back to farming the rest.
        uint32 budget = bot->GetMoney();
        uint32 failedBid = (uint32)AI_VALUE2(int, "manual int", "order buy failed bid");

        struct Candidate
        {
            uint32 auctionId = 0;
            uint32 itemGuid = 0;
            uint32 buyout = 0;
            uint32 count = 0;
        };
        Candidate best;

        for (auto const& auctionEntry : *auctionHouse->GetAuctions())
        {
            AuctionEntry* auction = auctionEntry.second;
            if (!auction || auction->buyout == 0)
                continue;

            if (auction->owner == bot->GetGUIDLow())
                continue;

            if (auction->Id == failedBid)
                continue;

            Item* item = sAuctionMgr.GetAItem(auction->itemGuidLow);
            if (!item || item->GetProto()->ItemId != order.itemId)
                continue;

            uint32 auctionCount = item->GetCount();
            if (auctionCount == 0 || auctionCount > missing)
                continue;

            if (auction->buyout > budget)
                continue;

            ItemPosCountVec dest;
            if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, order.itemId, auctionCount) != EQUIP_ERR_OK)
                continue;

            if (!best.auctionId ||
                BuyoutBeatsPerItem(auction->buyout, auctionCount, best.buyout, best.count))
                best = { auction->Id, auction->itemGuidLow, auction->buyout, auctionCount };
        }

        if (!best.auctionId)
        {
            // Nothing affordable that fits the bags: farm the rest.
            visitDone = true;
        }
        else
        {
            WorldPacket packet(CMSG_AUCTION_PLACE_BID, 8 + 4 + 4);
            packet << auctioneer->GetObjectGuid() << best.auctionId << best.buyout;
            bot->GetSession()->HandleAuctionPlaceBid(packet);

            bought = auctionHouse->GetAuction(best.auctionId) == nullptr &&
                sAuctionMgr.GetAItem(best.itemGuid) == nullptr;

            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(order.itemId);
            if (bought && proto)
                sPlayerbotAIConfig.logEvent(ai, "GuildAhBuy", proto->Name1, std::to_string(proto->ItemId));
            std::ostringstream out;
            out << (bought ? "Bought " : "Could not buy ") << best.count << "x ";
            out << (proto ? proto->Name1 : "item #" + std::to_string(order.itemId));
            if (bought)
            {
                SET_AI_VALUE2(int, "manual int", "order buy failed bid", 0);
                out << " from AH for guild order (" << (best.buyout / 10000) << "g)";
            }
            else
            {
                // A failed bid is never retried: the listing is gone or the
                // core refused it, so the rest becomes a farm.
                SET_AI_VALUE2(int, "manual int", "order buy failed bid", (int)best.auctionId);
                visitDone = true;
                out << " from AH for guild order; core rejected purchase (" << (best.buyout / 10000) << "g)";
            }
            ai->TellPlayerNoFacing(GetMaster(), out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }
    }

    ahLock.unlock();

    if (bought)
    {
        std::optional<TortoiseBots::GuildOrders::OrderRecord> after =
            TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
        uint32 deliveredAfter = (after && after->itemId == order.itemId) ? after->delivered : 0;
        if (!BuyStillMissing(order.amount, BuyOwnedCount(deliveredAfter,
                ai->GetInventoryItemsCountWithId(order.itemId), OrderMailboxItems(bot, order.itemId))))
            visitDone = true;
    }

    if (visitDone)
    {
        if (!bought)
            TortoiseBots::GuildOrders::NoteFarmRest(bot->GetGUIDLow());
        TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");
        if (!ai->HasActivePlayerMaster() && target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
        {
            sTravelMgr.SetNullTravelTarget(target);
            RESET_AI_VALUE(bool, "travel target active");
        }
    }
    return bought;
}
