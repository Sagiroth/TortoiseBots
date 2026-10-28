#include "playerbot/playerbot.h"
#include "GuildShareAhBuyAction.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "runtime/GuildOrderStore.h"
#include "playerbot/RandomItemMgr.h"
#include "AuctionHouse/AuctionHouseMgr.h"
#include "Mail/Mail.h"
#include "MapNodes/MasterPlayer.h"

using namespace ai;

bool GuildShareAhBuyAction::isUseful()
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

    uint32 money = bot->GetMoney();
    if (money < 100)
        return false;

    if (AI_VALUE(uint8, "bag space") > 90)
        return false;

    return true;
}

Unit* GuildShareAhBuyAction::FindNearbyAuctioneer()
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

uint32 GuildShareAhBuyAction::CountMailboxItems(uint32 itemId)
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

std::map<uint32, uint32> GuildShareAhBuyAction::GetNeededItems()
{
    std::map<uint32, uint32> needed;

    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (order.type != GuildOrderType::AuctionHouse || !order.itemId)
        return needed;
    if (!OrderStillNeedsWork(bot, order))
        return needed;

    uint32 itemId = order.itemId;

    std::optional<TortoiseBots::GuildOrders::OrderRecord> stored =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    uint32 delivered = (stored && stored->itemId == itemId) ? stored->delivered : 0;
    uint32 owned = delivered + ai->GetInventoryItemsCountWithId(itemId) + CountMailboxItems(itemId);
    uint32 want = order.amount ? order.amount : 1;
    if (owned >= want)
        return needed;

    needed[itemId] = want - owned;
    return needed;
}

bool GuildShareAhBuyAction::Execute(Event& event)
{
    Unit* auctioneer = FindNearbyAuctioneer();
    if (!auctioneer)
        return false;

    if (!sRandomBotFacade.m_ahActionMutex.try_lock())
        return false;

    bool bought = false;

    AuctionHouseEntry const* ahEntry = AuctionHouseMgr::GetAuctionHouseEntry(auctioneer);
    if (!ahEntry)
    {
        sRandomBotFacade.m_ahActionMutex.unlock();
        return false;
    }

    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(ahEntry);
    if (!auctionHouse)
    {
        sRandomBotFacade.m_ahActionMutex.unlock();
        return false;
    }

    std::map<uint32, uint32> neededItems = GetNeededItems();
    if (neededItems.empty())
    {
        sRandomBotFacade.m_ahActionMutex.unlock();
        return false;
    }

    uint32 availableBudget = AI_VALUE2(uint32, "free money for", uint32(NeedMoneyFor::guild));

    struct AuctionCandidate
    {
        uint32 auctionId;
        uint32 itemGuid;
        uint32 itemId;
        uint32 buyout;
        uint32 count;
    };

    AuctionCandidate bestCandidate = { 0, 0, 0, 0, 0 };
    uint32 bestPricePerItem = std::numeric_limits<uint32>::max();

    for (auto const& auctionEntry : *auctionHouse->GetAuctions())
    {
            AuctionEntry* auction = auctionEntry.second;
            if (!auction || auction->buyout == 0)
                continue; // Skip auctions with no buyout

            if (auction->owner == bot->GetGUIDLow())
                continue;

            Item* item = sAuctionMgr.GetAItem(auction->itemGuidLow);
            if (!item)
                continue;

            uint32 auctionItemId = item->GetProto()->ItemId;
            uint32 auctionCount = item->GetCount();

            if (sRandomItemMgr.IsTransmogClone(auctionItemId))
                continue;

            auto it = neededItems.find(auctionItemId);
            if (it == neededItems.end())
                continue;

            // Don't buy more than we actually need
            if (auctionCount > it->second)
                continue;

            if (auction->buyout > availableBudget)
                continue;

            ItemPosCountVec dest;
            InventoryResult msg = bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, auctionItemId, auctionCount);
            if (msg != EQUIP_ERR_OK)
                continue;

            uint32 pricePerItem = auction->buyout / auctionCount;

            if (pricePerItem < bestPricePerItem)
            {
                bestPricePerItem = pricePerItem;
                bestCandidate = { auction->Id, auction->itemGuidLow, auctionItemId, auction->buyout, auctionCount };
            }
    }

    if (bestCandidate.auctionId)
    {
        // Use the same validated core handler as a real client. The old
        // AuctionEntry::UpdateBid compatibility method is a no-op, so it
        // falsely reported a purchase without charging gold or delivering
        // mail.
        WorldPacket packet(CMSG_AUCTION_PLACE_BID, 8 + 4 + 4);
        packet << auctioneer->GetObjectGuid() << bestCandidate.auctionId << bestCandidate.buyout;
        bot->GetSession()->HandleAuctionPlaceBid(packet);

        bought = auctionHouse->GetAuction(bestCandidate.auctionId) == nullptr &&
            sAuctionMgr.GetAItem(bestCandidate.itemGuid) == nullptr;

        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(bestCandidate.itemId);
        if (bought && proto)
            sPlayerbotAIConfig.logEvent(ai, "GuildAhBuy", proto->Name1, std::to_string(proto->ItemId));
        std::ostringstream out;
        out << (bought ? "Bought " : "Could not buy ") << bestCandidate.count << "x ";
        if (proto)
            out << proto->Name1;
        else
            out << "item #" << bestCandidate.itemId;
        out << (bought ? " from AH for guild order (" : " from AH for guild order; core rejected purchase (")
            << (bestCandidate.buyout / 10000) << "g)";

        ai->TellPlayerNoFacing(GetMaster(), out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

    }

    sRandomBotFacade.m_ahActionMutex.unlock();
    return bought;
}
