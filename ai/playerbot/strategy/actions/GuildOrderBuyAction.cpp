#include "playerbot/playerbot.h"
#include "GuildOrderBuyAction.h"
#include "playerbot/GuildOrderCraft.h"
#include "playerbot/GuildOrderSources.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/TravelMgr.h"
#include "MailAction.h"
#include "runtime/GuildOrderBuyPolicy.h"
#include "runtime/GuildOrderRefundPolicy.h"
#include "runtime/GuildOrderStore.h"
#include "runtime/GuildOrderSupplyPolicy.h"
#include <algorithm>
#include <climits>
#include <sstream>

using namespace ai;
using namespace TortoiseBots::GuildOrders;

namespace
{

bool IsUnlimitedVendorItem(uint32_t itemId)
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    return proto && ItemUsageValue::IsItemSoldByAnyVendor(proto) &&
        !ItemUsageValue::IsItemSoldByAnyVendorButHasLimitedMaxCount(proto);
}

} // namespace

void ai::AdvanceGuildOrderAfterAuction(PlayerbotAI* ai, Player* bot, OrderRecord const& order)
{
    if (order.type == OrderType::Buy && !order.farmRest)
        NoteFarmRest(bot->GetGUIDLow());

    SourcePlan plan = ai::PlanGuildOrderSources(bot, order.supplyItemId, OrderType::Farm);
    SupplyOptions options;
    options.bankAttempted = true;
    options.auctionHouseAttempted = true;
    options.selfCanFarm = (order.type == OrderType::Craft && IsUnlimitedVendorItem(order.supplyItemId)) ||
        (plan.verdict == OrderReason::None && !plan.zones.empty());
    SupplyStep next = NextSupplyStep(options);
    if (next == SupplyStep::Sub)
    {
        if (RequestSupply(order.botGuidLow, order.supplyItemId, order.supplyCount))
        {
            SetSupply(order.botGuidLow, SupplyStep::Sub, order.supplyItemId,
                order.supplyCount, SupplyStep::None, order.reason);
            return;
        }
        SetNeedItems(order.botGuidLow, { NeedItem{ order.supplyItemId, order.supplyCount } },
            order.reason == OrderReason::BankDenied);
        return;
    }
    if (next == SupplyStep::Need)
    {
        SetNeedItems(order.botGuidLow, { NeedItem{ order.supplyItemId, order.supplyCount } },
            order.reason == OrderReason::BankDenied);
        return;
    }
    SetSupply(order.botGuidLow, next, order.supplyItemId, order.supplyCount,
        SupplyStep::None, order.reason);
}

bool GuildOrderBuyAction::isUseful()
{
    if (!bot->GetGuildId() || bot->IsInCombat())
        return false;

    std::optional<OrderRecord> order = Find(bot->GetGUIDLow());
    if (!order || order->guildId != bot->GetGuildId() || order->state != OrderState::Working ||
        order->supplyStep != SupplyStep::AuctionHouse || !order->supplyItemId || !order->supplyCount)
        return false;
    // No bag-space gate here: with nothing listed the leg must still move
    // down the ladder; a listing that does not fit is skipped in Execute.
    return FindNearbyAuctioneer() != nullptr;
}

Unit* GuildOrderBuyAction::FindNearbyAuctioneer()
{
    std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
    for (ObjectGuid& guid : npcs)
    {
        Unit* npc = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_AUCTIONEER);
        if (npc)
            return npc;
    }
    return nullptr;
}

bool GuildOrderBuyAction::Execute(Event& event)
{
    (void)event;
    uint32_t botGuidLow = bot->GetGUIDLow();
    std::optional<OrderRecord> stored = Find(botGuidLow);
    if (!stored || stored->guildId != bot->GetGuildId() || stored->state != OrderState::Working ||
        stored->supplyStep != SupplyStep::AuctionHouse || !stored->supplyItemId || !stored->supplyCount)
        return false;

    Unit* auctioneer = FindNearbyAuctioneer();
    if (!auctioneer)
        return false;

    uint32_t itemId = stored->supplyItemId;
    // The leg's count is already what is still missing after bags and
    // mailbox (the ladder refresh nets both), and a purchase moves the leg
    // to the mailbox at once, so counting the bags again here would cancel
    // a leg that still needs items.
    uint32_t missing = stored->supplyCount;
    if (!missing)
    {
        SetSupply(botGuidLow, SupplyStep::None, 0, 0);
        return false;
    }

    std::unique_lock<std::mutex> ahLock(sRandomBotFacade.m_ahActionMutex, std::try_to_lock);
    if (!ahLock.owns_lock())
        return false;

    AuctionHouseEntry const* ahEntry = AuctionHouseMgr::GetAuctionHouseEntry(auctioneer);
    AuctionHouseObject* auctionHouse = ahEntry ? sAuctionMgr.GetAuctionsMap(ahEntry) : nullptr;
    struct Candidate
    {
        uint32_t auctionId = 0;
        uint32_t itemGuid = 0;
        uint32_t buyout = 0;
        uint32_t count = 0;
    };
    Candidate best;
    uint32_t cash = bot->GetMoney();
    uint32_t heldGuildGold = std::min(stored->guildGoldHeld, cash);
    uint32_t ownedGold = cash - heldGuildGold;
    uint32_t capRemaining = BranchGoldCapRemaining(stored->botGuidLow);
    uint64_t maxAffordableWide = uint64_t(cash) + capRemaining;
    uint32_t maxAffordable = static_cast<uint32_t>(std::min<uint64_t>(maxAffordableWide, UINT32_MAX));

    if (auctionHouse)
    {
        uint32_t failedBid = uint32_t(AI_VALUE2(int, "manual int", "order buy failed bid"));
        for (auto const& auctionEntry : *auctionHouse->GetAuctions())
        {
            AuctionEntry* auction = auctionEntry.second;
            if (!auction || !auction->buyout || auction->owner == botGuidLow || auction->Id == failedBid ||
                auction->buyout > maxAffordable)
                continue;
            Item* item = sAuctionMgr.GetAItem(auction->itemGuidLow);
            if (!item || item->GetProto()->ItemId != itemId)
                continue;
            uint32_t count = item->GetCount();
            if (!count || count > missing)
                continue;
            ItemPosCountVec dest;
            if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count) != EQUIP_ERR_OK)
                continue;
            if (!best.auctionId || BuyoutBeatsPerItem(auction->buyout, count, best.buyout, best.count))
                best = { auction->Id, auction->itemGuidLow, auction->buyout, count };
        }
    }

    if (!best.auctionId)
    {
        ahLock.unlock();
        AdvanceGuildOrderAfterAuction(ai, bot, *stored);
        RESET_AI_VALUE(bool, "travel target active");
        return false;
    }

    if (best.buyout > cash)
    {
        uint32_t shortfall = best.buyout - cash;
        uint32_t cappedShortfall = GuildGoldShortfall(best.buyout, ownedGold, heldGuildGold, capRemaining);
        // One top-up try per listing: the bank leg already failed for the
        // remembered auction id, so the ladder moves down instead of walking
        // back to the banker for the same listing. A new listing may try
        // again; the bank may have funds by then.
        int triedAuction = AI_VALUE2(int, "manual int", "order buy gold tried");
        if (GoldTopUpAllowed(best.auctionId, uint32_t(triedAuction), cappedShortfall,
            sPlayerbotAIConfig.guildOrderBankGold) &&
            cappedShortfall >= shortfall && shortfall > 0 &&
            uint64_t(cash) + shortfall <= uint32_t(INT32_MAX))
        {
            ahLock.unlock();
            SET_AI_VALUE2(int, "manual int", "order buy gold tried", int(best.auctionId));
            SetSupply(botGuidLow, SupplyStep::Bank, itemId, missing, SupplyStep::AuctionHouse,
                OrderReason::None, shortfall);
            return false;
        }
        ahLock.unlock();
        AdvanceGuildOrderAfterAuction(ai, bot, *stored);
        RESET_AI_VALUE(bool, "travel target active");
        return false;
    }

    WorldPacket packet(CMSG_AUCTION_PLACE_BID, 8 + 4 + 4);
    packet << auctioneer->GetObjectGuid() << best.auctionId << best.buyout;
    bot->GetSession()->HandleAuctionPlaceBid(packet);
    bool bought = auctionHouse->GetAuction(best.auctionId) == nullptr &&
        sAuctionMgr.GetAItem(best.itemGuid) == nullptr;
    ahLock.unlock();

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (bought)
    {
        sPlayerbotAIConfig.logEvent(ai, "GuildAhBuy", proto ? proto->Name1 : "item", std::to_string(itemId));
        uint32_t guildUsed = best.buyout > ownedGold ? best.buyout - ownedGold : 0;
        ConsumeGuildGoldHeld(botGuidLow, std::min(guildUsed, heldGuildGold));
        NoteSupplyGain(botGuidLow, itemId, best.count);
        NoteSupplyProgress(botGuidLow);
        SET_AI_VALUE2(int, "manual int", "order buy gold tried", 0);
        SetSupply(botGuidLow, SupplyStep::Bags, itemId, missing, SupplyStep::AuctionHouse, stored->reason);
    }
    else
    {
        SET_AI_VALUE2(int, "manual int", "order buy failed bid", int(best.auctionId));
        AdvanceGuildOrderAfterAuction(ai, bot, *stored);
    }

    std::ostringstream out;
    out << (bought ? "Bought " : "Could not buy ") << best.count << "x ";
    out << (proto ? proto->Name1 : "item #" + std::to_string(itemId));
    out << (bought ? " from AH for guild order (" : " from AH for guild order; purchase failed (");
    out << (best.buyout / 10000) << "g)";
    ai->TellPlayerNoFacing(GetMaster(), out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    return bought;
}

bool GuildOrderTakeMailAction::isUseful()
{
    if (!bot->GetGuildId() || bot->IsInCombat())
        return false;
    std::optional<OrderRecord> stored = Find(bot->GetGUIDLow());
    if (!stored || stored->state != OrderState::Working || stored->supplyStep != SupplyStep::Bags)
        return false;
    uint32_t itemId = TortoiseBots::GuildOrders::SupplyMailItemId(stored->supplyStep,
        stored->supplyItemId, stored->itemId);
    if (!itemId || !OrderMailboxItems(bot, itemId) || AI_VALUE(uint8, "bag space") > 95)
        return false;
    // Only at the mailbox, not while walking past one: the core refuses the
    // take past interact range, and a refused take still costs a server
    // round trip every tick of the walk. FindMailbox scans sight range
    // (60 yd), far past the 5 yd interact check the handler enforces.
    ObjectGuid mailbox = MailProcessor::FindMailbox(ai);
    if (mailbox.IsEmpty())
        return false;
    return bot->GetGameObjectIfCanInteractWith(mailbox, GAMEOBJECT_TYPE_MAILBOX) != nullptr;
}

bool GuildOrderTakeMailAction::Execute(Event& event)
{
    (void)event;
    std::optional<OrderRecord> stored = Find(bot->GetGUIDLow());
    if (!stored || stored->state != OrderState::Working || stored->supplyStep != SupplyStep::Bags)
        return false;
    uint32_t itemId = TortoiseBots::GuildOrders::SupplyMailItemId(stored->supplyStep,
        stored->supplyItemId, stored->itemId);
    uint32_t before = itemId ? ai->GetInventoryItemsCountWithId(itemId) : 0;
    Event take("rpg action", "take");
    bool result = ai->DoSpecificAction("mail", take, true);
    if (itemId)
    {
        uint32_t after = ai->GetInventoryItemsCountWithId(itemId);
        if (after > before)
        {
            NoteSupplyGain(bot->GetGUIDLow(), itemId, after - before);
            NoteSupplyProgress(bot->GetGUIDLow());
        }
    }
    if (!OrderMailboxItems(bot, itemId))
    {
        ClearMailEmpty(bot->GetGUIDLow());
        SupplyStep next = stored->supplyReturnStep;
        SetSupply(bot->GetGUIDLow(), next, next == SupplyStep::None ? 0 : itemId, 0,
            SupplyStep::None, stored->reason);
    }
    return result;
}
