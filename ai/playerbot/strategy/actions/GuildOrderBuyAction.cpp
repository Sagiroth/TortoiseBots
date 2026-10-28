
#include "playerbot/playerbot.h"
#include "GuildOrderBuyAction.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "runtime/GuildOrderBuyPolicy.h"
#include "runtime/GuildOrderStore.h"
#include "playerbot/RandomItemMgr.h"
#include "AuctionHouse/AuctionHouseMgr.h"
#include "Mail/Mail.h"
#include "MapNodes/MasterPlayer.h"

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

    if (bot->GetMoney() == 0)
        return false;

    if (AI_VALUE(uint8, "bag space") > 90)
        return false;

    return true;
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

uint32 GuildOrderBuyAction::CountMailboxItems(uint32 itemId)
{
    uint32 count = 0;
    time_t curTime = time(nullptr);
    MasterPlayer* master = bot->GetSession() ? bot->GetSession()->GetMasterPlayer() : nullptr;
    if (!master)
        return 0;

    for (PlayerMails::iterator itr = master->GetMailBegin(); itr != master->GetMailEnd(); ++itr)
    {
        Mail* mail = *itr;
        if (!mail || mail->state == MAIL_STATE_DELETED || curTime < mail->deliver_time)
            continue;

        if (!mail->has_items)
            continue;

        for (MailItemInfoVec::const_iterator itemItr = mail->items.begin(); itemItr != mail->items.end(); ++itemItr)
        {
            if (itemItr->item_template == itemId)
            {
                Item* mailItem = master->GetMItem(itemItr->item_guid);
                count += mailItem ? mailItem->GetCount() : 1;
            }
        }
    }

    return count;
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
        CountMailboxItems(order.itemId));
    uint32 missing = BuyStillMissing(order.amount, owned);
    if (!missing)
        return false;

    if (!sRandomBotFacade.m_ahActionMutex.try_lock())
        return false;

    bool bought = false;
    bool nothingToBuy = false;

    AuctionHouseEntry const* ahEntry = AuctionHouseMgr::GetAuctionHouseEntry(auctioneer);
    AuctionHouseObject* auctionHouse = ahEntry ? sAuctionMgr.GetAuctionsMap(ahEntry) : nullptr;
    if (auctionHouse)
    {
        uint32 budget = AI_VALUE2(uint32, "free money for", uint32(NeedMoneyFor::guild));

        struct Candidate
        {
            uint32 auctionId = 0;
            uint32 itemGuid = 0;
            uint32 buyout = 0;
            uint32 count = 0;
        };
        Candidate best;
        bool affordableSeen = false;

        for (auto const& auctionEntry : *auctionHouse->GetAuctions())
        {
            AuctionEntry* auction = auctionEntry.second;
            if (!auction || auction->buyout == 0)
                continue;

            if (auction->owner == bot->GetGUIDLow())
                continue;

            Item* item = sAuctionMgr.GetAItem(auction->itemGuidLow);
            if (!item || item->GetProto()->ItemId != order.itemId)
                continue;

            uint32 auctionCount = item->GetCount();
            if (auctionCount == 0 || auctionCount > missing)
                continue;

            if (auction->buyout > budget)
                continue;
            affordableSeen = true;

            ItemPosCountVec dest;
            if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, order.itemId, auctionCount) != EQUIP_ERR_OK)
                continue;

            if (!best.auctionId ||
                BuyoutBeatsPerItem(auction->buyout, auctionCount, best.buyout, best.count))
                best = { auction->Id, auction->itemGuidLow, auction->buyout, auctionCount };
        }

        // No gold or no listings: the rest becomes a farm. Bag room gone
        // means a partial delivery instead (the farm path reports it).
        nothingToBuy = !affordableSeen;

        if (best.auctionId)
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
            out << (bought ? " from AH for guild order (" : " from AH for guild order; core rejected purchase (")
                << (best.buyout / 10000) << "g)";
            ai->TellPlayerNoFacing(GetMaster(), out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }
    }

    sRandomBotFacade.m_ahActionMutex.unlock();

    if (nothingToBuy)
        TortoiseBots::GuildOrders::NoteFarmRest(bot->GetGUIDLow());
    return bought;
}
