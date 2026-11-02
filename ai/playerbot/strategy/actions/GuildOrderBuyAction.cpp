#include "playerbot/playerbot.h"
#include "GuildOrderBuyAction.h"
#include "playerbot/GuildOrderCraft.h"
#include "playerbot/GuildOrderSources.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/TravelMgr.h"
#include "MailAction.h"
#include "runtime/GuildOrderAuctionPolicy.h"
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

void ai::AdvanceGuildOrderAfterAuction(PlayerbotAI* ai, Player* bot, OrderRecord const& order,
    OrderReason reason)
{
    if (order.type == OrderType::Buy && !order.farmRest)
        NoteFarmRest(bot->GetGUIDLow());
    // Leaving the auction leg ends any relist it still owed: the surplus
    // stays in the bags for the delivery leftover path.
    ClearRelistSurplus(order.botGuidLow);

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
                order.supplyCount, SupplyStep::None, reason);
            return;
        }
        SetNeedItems(order.botGuidLow, { NeedItem{ order.supplyItemId, order.supplyCount } },
            reason == OrderReason::BankDenied);
        return;
    }
    if (next == SupplyStep::Need)
    {
        SetNeedItems(order.botGuidLow, { NeedItem{ order.supplyItemId, order.supplyCount } },
            reason == OrderReason::BankDenied);
        return;
    }
    SetSupply(order.botGuidLow, next, order.supplyItemId, order.supplyCount,
        SupplyStep::None, reason);
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
    return GuildOrderAuctioneerNear(ai, bot) != nullptr;
}

Unit* ai::GuildOrderAuctioneerNear(PlayerbotAI* ai, Player* bot)
{
    std::list<ObjectGuid> npcs =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("nearest npcs")->Get();
    for (ObjectGuid& guid : npcs)
    {
        Creature* npc = ai->GetCreature(guid);
        if (!npc || !npc->IsAlive() || !npc->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_AUCTIONEER) ||
            npc->IsHostileTo(bot) || bot->GetDistance(npc) > kGuildOrderAuctioneerNearYd)
            continue;
        return npc;
    }
    return nullptr;
}

Unit* ai::GuildOrderAuctioneerInReach(PlayerbotAI* ai, Player* bot)
{
    std::list<ObjectGuid> npcs =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("nearest npcs")->Get();
    for (ObjectGuid& guid : npcs)
    {
        Unit* npc = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_AUCTIONEER);
        if (npc)
            return npc;
    }
    return nullptr;
}

Unit* GuildOrderBuyAction::FindNearbyAuctioneer()
{
    return ai::GuildOrderAuctioneerInReach(ai, bot);
}

namespace
{

// The stack the relist can post without eating into what the order still
// needs: a whole stack that fits inside the pending surplus, else the
// smallest stack (which the caller splits down to the pending amount).
// Null when no stack carries the item.
Item* FindRelistStack(Player* bot, uint32_t itemId, uint32_t pendingCount)
{
    Item* smallest = nullptr;
    Item* fits = nullptr;
    auto consider = [&](Item* item)
    {
        if (!item || item->GetEntry() != itemId)
            return;
        if (item->GetCount() <= pendingCount && (!fits || item->GetCount() > fits->GetCount()))
            fits = item;
        if (!smallest || item->GetCount() < smallest->GetCount())
            smallest = item;
    };
    for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        consider(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8_t bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* pack = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (!pack)
            continue;
        for (uint32_t slot = 0; slot < pack->GetBagSize(); ++slot)
            consider(pack->GetItemByPos(uint8_t(slot)));
    }
    return fits ? fits : smallest;
}

// Lowest per-unit buyout already listed for the item, 0 when none. The
// caller holds the auction mutex.
uint32_t LowestListingUnitPrice(AuctionHouseObject* auctionHouse, uint32_t itemId)
{
    uint32_t lowest = 0;
    if (!auctionHouse)
        return 0;
    for (auto const& entry : *auctionHouse->GetAuctions())
    {
        AuctionEntry* auction = entry.second;
        if (!auction || !auction->buyout)
            continue;
        Item* item = sAuctionMgr.GetAItem(auction->itemGuidLow);
        if (!item || item->GetProto()->ItemId != itemId || !item->GetCount())
            continue;
        uint32_t const unit = AuctionUnitPrice(auction->buyout, item->GetCount());
        if (unit && (!lowest || unit < lowest))
            lowest = unit;
    }
    return lowest;
}

// A free inventory position, so a split lands in its own stack instead of
// merging into an existing one and hiding how much was set aside.
bool FindFreeBagSlot(Player* bot, uint8_t& bag, uint8_t& slot)
{
    for (uint8_t s = INVENTORY_SLOT_ITEM_START; s < INVENTORY_SLOT_ITEM_END; ++s)
    {
        if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, s))
        {
            bag = INVENTORY_SLOT_BAG_0;
            slot = s;
            return true;
        }
    }
    for (uint8_t b = INVENTORY_SLOT_BAG_START; b < INVENTORY_SLOT_BAG_END; ++b)
    {
        Bag* pack = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, b);
        if (!pack)
            continue;
        for (uint32_t s = 0; s < pack->GetBagSize(); ++s)
        {
            if (!pack->GetItemByPos(uint8_t(s)))
            {
                bag = b;
                slot = uint8_t(s);
                return true;
            }
        }
    }
    return false;
}

// Posts up to one stack of the pending surplus. Returns what left the bags
// (0 = nothing posted; the caller then leaves the surplus to the delivery
// leftover path). The caller holds the auction mutex and has checked the
// auctioneer.
uint32_t RelistSurplus(PlayerbotAI* ai, Player* bot, Unit* auctioneer, uint32_t itemId,
    uint32_t pendingCount, uint32_t boughtUnitPrice)
{
    if (!auctioneer || !itemId || !pendingCount)
        return 0;
    AuctionHouseEntry const* ahEntry = bot->GetSession()->GetCheckedAuctionHouseForAuctioneer(
        auctioneer->getObjectGuid());
    AuctionHouseObject* auctionHouse = ahEntry ? sAuctionMgr.GetAuctionsMap(ahEntry) : nullptr;
    if (!ahEntry || !auctionHouse)
        return 0;
    Item* stack = FindRelistStack(bot, itemId, pendingCount);
    if (!stack)
        return 0;
    uint32_t postCount = stack->GetCount() <= pendingCount ? stack->GetCount() :
        std::min(pendingCount, 255u);
    // The split packet carries the stack in a single byte; post a larger
    // surplus over several visits instead of overflowing it.
    if (postCount > 255)
        postCount = 255;
    if (stack->GetCount() > postCount)
    {
        uint8_t dstBag = 0, dstSlot = 0;
        if (!FindFreeBagSlot(bot, dstBag, dstSlot))
            return 0;
        uint16_t const src = stack->GetPos();
        WorldPacket split(CMSG_SPLIT_ITEM, 5);
        split << uint8_t(src >> 8) << uint8_t(src & 0xFF) << dstBag << dstSlot << uint8_t(postCount);
        bot->GetSession()->HandleSplitItemOpcode(split);
        stack = bot->GetItemByPos(dstBag, dstSlot);
        if (!stack || stack->GetEntry() != itemId || stack->GetCount() != postCount)
            return 0;
    }

    uint32_t const unitPrice = RelistUnitPriceFromUnit(boughtUnitPrice,
        LowestListingUnitPrice(auctionHouse, itemId));
    uint32_t const buyout = RelistTotal(unitPrice, postCount);
    if (!buyout)
        return 0;
    uint32_t const deposit = AuctionHouseMgr::GetAuctionDeposit(ahEntry, kRelistDurationMinutes * MINUTE,
        stack);
    if (bot->GetMoney() < deposit)
        return 0;

    ObjectGuid const itemGuid = stack->getObjectGuid();
    uint32_t const bid = buyout > 1 ? uint32_t(uint64_t(buyout) * 95 / 100) : 1;
    WorldPacket packet(CMSG_AUCTION_SELL_ITEM, 8 + 8 + 4 + 4 + 4);
    packet << auctioneer->getObjectGuid() << itemGuid;
    packet << bid << buyout << kRelistDurationMinutes;
    bot->GetSession()->HandleAuctionSellItem(packet);
    if (bot->GetItemByGuid(itemGuid))
        return 0;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    sPlayerbotAIConfig.logEvent(ai, "GuildAhRelist", proto ? proto->Name1 : "item",
        std::to_string(itemId) + ":" + std::to_string(postCount) + ":" + std::to_string(buyout));
    // The posting carries guild-funded value: the sale gold or the returned
    // stack comes back by mail later and the ledger keeps it owed to the
    // guild until the bank trip pays it back.
    NoteRelistPosted(bot->GetGUIDLow(), bot->GetGuildId(), itemId, postCount, unitPrice, bid, buyout,
        deposit, kRelistDurationMinutes * 60);
    return postCount;
}

} // namespace

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
    {
        Unit* nearby = GuildOrderAuctioneerNear(ai, bot);
        return nearby && MoveNear(nearby, INTERACTION_DISTANCE - 1.0f);
    }

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

    // An oversized purchase left a surplus to relist: this visit posts it
    // instead of buying more. The ladder refresh only routes here once the
    // bought stack is in the bags (mail picked up).
    uint32_t relistItemId = 0, relistCount = 0, relistUnitPrice = 0;
    if (PendingRelist(botGuidLow, relistItemId, relistCount, relistUnitPrice) &&
        relistItemId == itemId && relistCount)
    {
        uint32_t const posted = RelistSurplus(ai, bot, auctioneer, itemId, relistCount, relistUnitPrice);
        uint32_t const remaining = posted && posted < relistCount ? relistCount - posted : 0;
        if (remaining)
            NoteRelistSurplus(botGuidLow, itemId, remaining, relistUnitPrice);
        else
            ClearRelistSurplus(botGuidLow);
        ahLock.unlock();
        if (!posted)
        {
            // No AH access, no free split slot, or the deposit is
            // unaffordable: the pending relist is dropped and the surplus
            // stays in the bags for the order's delivery leftover path (the
            // guild-bank deposit).
            if (SurplusDestinationAfterRelist(false, relistCount) == SurplusDestination::DeliveryLeftover)
            {
                ItemPrototype const* failedProto = sObjectMgr.GetItemPrototype(itemId);
                sPlayerbotAIConfig.logEvent(ai, "GuildAhRelistFailed",
                    failedProto ? failedProto->Name1 : "item", std::to_string(itemId));
            }
            SetSupply(botGuidLow, SupplyStep::None, 0, 0);
            RESET_AI_VALUE(bool, "travel target active");
            return false;
        }
        NoteSupplyProgress(botGuidLow);
        if (remaining)
            SetSupply(botGuidLow, SupplyStep::AuctionHouse, itemId, remaining, SupplyStep::None,
                stored->reason);
        else
            SetSupply(botGuidLow, SupplyStep::None, 0, 0);
        return true;
    }

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
            if (!count)
                continue;
            ItemPosCountVec dest;
            if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count) != EQUIP_ERR_OK)
                continue;
            // A stack bigger than the miss is allowed: the order uses what
            // it needs and the surplus returns to the AH. The smallest
            // covering stack wins, then the cheapest per unit.
            if (!best.auctionId ||
                AuctionPickBeats(count, auction->buyout, best.count, best.buyout, missing))
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
        uint32_t const surplus = BuySurplus(best.count, missing);
        if (surplus)
            NoteRelistSurplus(botGuidLow, itemId, surplus, AuctionUnitPrice(best.buyout, best.count));
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
    if (!itemId || !OrderMailboxItems(bot, itemId))
        return false;
    // No percent gate here on purpose: the take below fits only the order
    // item (stack merge included), so a bag full of unsellable stock still
    // lifts it. The broad take's two-free-slot rule would refuse forever.
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
    // Relist mail waiting for the guild settles first: the surplus stack the
    // order posted comes back tagged with the same item as the order leg, and
    // taking it as order stock would lose the ledger row.
    ai::TakeRelistMail(ai);
    std::optional<OrderRecord> stored = Find(bot->GetGUIDLow());
    if (!stored || stored->state != OrderState::Working || stored->supplyStep != SupplyStep::Bags)
        return false;
    uint32_t itemId = TortoiseBots::GuildOrders::SupplyMailItemId(stored->supplyStep,
        stored->supplyItemId, stored->itemId);
    if (!itemId)
        return false;
    uint32_t before = ai->GetInventoryItemsCountWithId(itemId);
    uint32_t taken = TakeOrderMail(ai, itemId);
    bool result = taken > 0;
    if (taken)
    {
        NoteSupplyGain(bot->GetGUIDLow(), itemId, taken);
        NoteSupplyProgress(bot->GetGUIDLow());
    }
    else if (ai->GetInventoryItemsCountWithId(itemId) > before)
    {
        uint32_t gained = ai->GetInventoryItemsCountWithId(itemId) - before;
        NoteSupplyGain(bot->GetGUIDLow(), itemId, gained);
        NoteSupplyProgress(bot->GetGUIDLow());
        result = true;
    }
    if (!OrderMailboxItems(bot, itemId))
    {
        ClearMailEmpty(bot->GetGUIDLow());
        SupplyStep next = stored->supplyReturnStep;
        SetSupply(bot->GetGUIDLow(), next, next == SupplyStep::None ? 0 : itemId, 0,
            SupplyStep::None, stored->reason);
    }
    else if (!result)
    {
        // The order item is still waiting but nothing more fits: park the
        // mailbox leg so the ladder (and the vendor loop behind it) can move
        // on instead of walking back to a mailbox it cannot empty.
        NoteMailEmpty(bot->GetGUIDLow(), itemId);
    }
    return result;
}
