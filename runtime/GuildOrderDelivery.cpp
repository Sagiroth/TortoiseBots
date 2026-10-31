#include "GuildOrderDelivery.h"

#include "GuildOrderDeliveryPolicy.h"
#include "GuildOrderDeliveryQueue.h"
#include "GuildOrderKeepPolicy.h"
#include "GuildOrderSupplyQueue.h"
#include "GuildOrderStore.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/GuildOrderCraft.h"
#include "../ai/playerbot/TravelMgr.h"
#include "PlayerbotAIStorage.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Guild/GuildMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "GuildBank/GuildBank.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DatabaseEnv.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Mail/Mail.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"

#include <list>
#include <algorithm>
#include <climits>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace TortoiseBots
{
namespace GuildOrders
{
namespace
{

// Entries the core accepts as guild bankers outright; anything else must
// carry the guild-banker gossip option (same list as the bank itself).
bool IsGuildBankerEntry(uint32_t entry)
{
    switch (entry)
    {
        case 62008:
        case 62009:
        case 62010:
        case 62011:
        case 62012:
        case 80917:
        case 80918:
            return true;
        default:
            return false;
    }
}

bool HasGuildBankerGossip(Creature* banker)
{
    if (!banker || !banker->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP))
        return false;
    GossipMenuItemsMapBounds menuItems =
        sObjectMgr.GetGossipMenuItemsMapBounds(banker->GetDefaultGossipMenuId());
    if (menuItems.first == menuItems.second)
        menuItems = sObjectMgr.GetGossipMenuItemsMapBounds(0);
    for (auto itr = menuItems.first; itr != menuItems.second; ++itr)
    {
        GossipMenuItems const& item = itr->second;
        if (item.option_id == GOSSIP_OPTION_GUILD_BANKER &&
            (item.npc_option_npcflag & banker->GetUInt32Value(UNIT_NPC_FLAGS)))
            return true;
    }
    return false;
}

bool IsGuildBanker(Creature* creature)
{
    return creature && (IsGuildBankerEntry(creature->GetEntry()) || HasGuildBankerGossip(creature));
}

Unit* FindNearbyGuildBanker(Player* bot, ::PlayerbotAI* ai)
{
    if (!bot || !ai)
        return nullptr;
    std::list<ObjectGuid> npcs =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("nearest npcs")->Get();
    for (ObjectGuid const& guid : npcs)
    {
        Unit* npc = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
        if (npc && npc->IsCreature() && IsGuildBanker(npc->ToCreature()))
            return npc;
    }
    return nullptr;
}

uint32_t NoBankerSearches(::PlayerbotAI* ai)
{
    if (!ai || !ai->GetAiObjectContext())
        return 0;
    int32 count = ai->GetAiObjectContext()->GetValue<int32>("manual int", "guild order no banker")->Get();
    return count > 0 ? uint32_t(count) : 0;
}

// Client numbering the bank message expects: backpack 0 with slots from 1,
// other bags 1-4 with slots from 1. Returns false for anything worn.
bool ClientBagSlot(Item* item, uint8_t& bag, uint8_t& slot)
{
    uint8_t serverBag = item->GetBagSlot();
    uint8_t serverSlot = item->GetSlot();
    if (serverBag == INVENTORY_SLOT_BAG_0)
    {
        if (serverSlot < INVENTORY_SLOT_ITEM_START || serverSlot >= INVENTORY_SLOT_ITEM_END)
            return false;
        bag = 0;
        slot = uint8_t(serverSlot - INVENTORY_SLOT_ITEM_START + 1);
        return true;
    }
    if (serverBag < INVENTORY_SLOT_BAG_START || serverBag >= INVENTORY_SLOT_BAG_END)
        return false;
    bag = uint8_t(serverBag - INVENTORY_SLOT_BAG_START + 1);
    slot = uint8_t(serverSlot + 1);
    return true;
}

bool ClientInventorySlot(uint16_t pos, uint8_t& bag, uint8_t& slot)
{
    uint8_t serverBag = uint8_t(pos >> 8);
    uint8_t serverSlot = uint8_t(pos & 0xFF);
    if (serverBag == INVENTORY_SLOT_BAG_0)
    {
        if (serverSlot < INVENTORY_SLOT_ITEM_START || serverSlot >= INVENTORY_SLOT_ITEM_END)
            return false;
        bag = 0;
        slot = uint8_t(serverSlot - INVENTORY_SLOT_ITEM_START + 1);
        return true;
    }
    if (serverBag < INVENTORY_SLOT_BAG_START || serverBag >= INVENTORY_SLOT_BAG_END)
        return false;
    bag = uint8_t(serverBag - INVENTORY_SLOT_BAG_START + 1);
    slot = uint8_t(serverSlot + 1);
    return true;
}

// Smallest carried stack of the item. The drain moves whole stacks,
// smallest first, so a partial stack fills the quota with the least
// overshoot; surplus stacks past the quota are never touched.
Item* SmallestStack(Player* bot, uint32_t itemId)
{
    if (!bot)
        return nullptr;
    Item* smallest = nullptr;
    for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item || item->GetEntry() != itemId)
            continue;
        if (!smallest || item->GetCount() < smallest->GetCount())
            smallest = item;
    }
    for (uint8_t bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* pack = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (!pack)
            continue;
        for (uint32_t slot = 0; slot < pack->GetBagSize(); ++slot)
        {
            Item* item = pack->GetItemByPos(uint8_t(slot));
            if (!item || item->GetEntry() != itemId)
                continue;
            if (!smallest || item->GetCount() < smallest->GetCount())
                smallest = item;
        }
    }
    return smallest;
}

// One pass at the banker: whole stacks, smallest first, one stack per
// tab, moving on when a tab did not move it. Stops once `delivered`
// reaches `amount` (overshoot at most one stack). Returns what left the
// bags. The drain calls this once per world tick; consecutive passes that
// move nothing mark the bank refused.
uint32_t DepositPass(Player* bot, Guild* guild, Unit* banker, uint32_t itemId,
                     uint32_t delivered, uint32_t amount)
{
    if (!bot || !guild || !guild->_Bank || !banker)
        return 0;
    ObjectGuid oldSelection = bot->GetSelectionGuid();
    bot->SetSelectionGuid(banker->getObjectGuid());
    uint32_t moved = 0;
    for (size_t tab = 0; tab < kDeliveryTabCount && delivered + moved < amount; ++tab)
    {
        Item* stack = SmallestStack(bot, itemId);
        if (!stack)
            break;
        uint8_t bag = 0, slot = 0;
        if (!ClientBagSlot(stack, bag, slot))
            break;
        uint32_t before = bot->GetItemCount(itemId);
        std::ostringstream msg;
        msg << "DepositItem:" << uint32_t(bag) << ':' << uint32_t(slot) << ':'
            << uint32_t(DeliveryTabAt(tab)) << ":0:" << stack->GetCount();
        guild->_Bank->HandleAddonMessages(msg.str(), bot);
        uint32_t after = bot->GetItemCount(itemId);
        if (after < before)
            moved += before - after;
    }
    bot->SetSelectionGuid(oldSelection);
    return moved;
}

ObjectGuid MailReceiverGuid(OrderRecord const& order, ObjectGuid botGuid)
{
    Guild* guild = sGuildMgr.GetGuildById(order.guildId);
    ObjectGuid leader = guild ? guild->GetLeaderGuid() : ObjectGuid();
    if (leader && leader != botGuid)
        return leader;
    ObjectGuid giver(HIGHGUID_PLAYER, order.giverGuidLow);
    if (order.giverGuidLow && giver != botGuid)
        return giver;
    return ObjectGuid();
}

// A leg change orphans the other leg's forced target: the banker target
// after a BackToWork delivery, the farm target when delivery starts. The
// target's own conditions no longer hold, but a forced target in TRAVEL
// ignores them, so expire it outright.
void ExpireTargetFor(::PlayerbotAI* ai)
{
    if (!ai || !ai->GetAiObjectContext())
        return;
    auto* holder = ai->GetAiObjectContext()->GetValue<ai::TravelTarget*>("travel target");
    ai::TravelTarget* target = holder ? holder->Get() : nullptr;
    if (!target || !target->IsActive())
        return;
    for (std::string const& condition : target->GetConditions())
    {
        if (condition == "has guild order delivery" || condition == "has guild travel order")
        {
            target->SetForced(false);
            target->SetStatus(ai::TravelStatus::TRAVEL_STATUS_EXPIRED);
            return;
        }
    }
}

// Mails whole stacks, smallest first, one stack per mail, until
// `delivered` reaches `amount` (overshoot at most one stack). Returns what
// left the bags and sets mailedAny when at least one mail went out.
// Surplus stacks past the quota stay in the bags.
uint32_t MailStacks(Player* bot, uint32_t itemId, ObjectGuid receiverGuid,
                    uint32_t delivered, uint32_t amount, bool& mailedAny)
{
    mailedAny = false;
    if (!bot || !receiverGuid || receiverGuid == bot->getObjectGuid())
        return 0;
    Player* receiver = sObjectMgr.GetPlayer(receiverGuid);
    uint32_t mailed = 0;
    while (delivered + mailed < amount)
    {
        Item* stack = SmallestStack(bot, itemId);
        if (!stack)
            break;
        std::ostringstream body;
        body << "Hello,\n\nHere are the ordered items your guild bank could not take.\n\n"
             << bot->GetName() << '\n';
        MailDraft draft("Guild order delivery", body.str());
        uint32_t count = stack->GetCount();
        bot->MoveItemFromInventory(stack->GetBagSlot(), stack->GetSlot(), true);
        stack->DeleteFromInventoryDB();
        stack->SetOwnerGuid(receiverGuid);
        // The owner change alone leaves the item UNCHANGED, in which case
        // SaveToDB writes nothing and an offline receiver keeps the bot as
        // owner in the database. A new item keeps ITEM_NEW so the save
        // inserts rather than updates its row.
        if (stack->GetState() != ITEM_NEW)
            stack->FSetState(ITEM_CHANGED);
        stack->SaveToDB();
        draft.AddItem(stack);
        draft.SendMailTo(MailReceiver(receiver, receiverGuid), MailSender(bot));
        mailed += count;
        mailedAny = true;
    }
    return mailed;
}

void CollectCraftReagents(Player* bot, uint32_t itemId, uint32_t depth,
    std::unordered_set<uint32_t>& visited, std::unordered_set<uint32_t>& reagents)
{
    if (!bot || !itemId || depth >= 8 || !visited.insert(itemId).second)
        return;
    uint32_t spellId = ai::FindCraftSpellFor(bot, itemId);
    SpellEntry const* spell = spellId ? sServerFacade.LookupSpellInfo(spellId) : nullptr;
    if (!spell)
        return;
    for (uint8_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!spell->ReagentCount[i] || spell->Reagent[i] <= 0)
            continue;
        uint32_t reagentId = uint32_t(spell->Reagent[i]);
        reagents.insert(reagentId);
        CollectCraftReagents(bot, reagentId, depth + 1, visited, reagents);
    }
}

void DeliverCraftLeftovers(Player* bot, Guild* guild, Unit* banker, OrderRecord const& order)
{
    if (!bot || !guild || order.type != OrderType::Craft || !order.itemId)
        return;
    std::unordered_set<uint32_t> visited;
    std::unordered_set<uint32_t> reagents;
    CollectCraftReagents(bot, order.itemId, 0, visited, reagents);
    for (uint32_t itemId : reagents)
    {
        if (itemId == order.itemId)
            continue;
        for (uint32_t pass = 0; pass < 200; ++pass)
        {
            uint32_t count = bot->GetItemCount(itemId);
            if (!count)
                break;
            uint32_t moved = banker ? DepositPass(bot, guild, banker, itemId, 0, count) : 0;
            if (moved)
                continue;
            ObjectGuid receiver = MailReceiverGuid(order, bot->getObjectGuid());
            if (receiver)
            {
                bool mailed = false;
                MailStacks(bot, itemId, receiver, 0, count, mailed);
            }
            break;
        }
    }
}

class DeliveryDrain
{
public:
    static DeliveryDrain& Instance()
    {
        static DeliveryDrain instance;
        return instance;
    }

    void Update()
    {
        for (PendingDelivery const& request : DeliveryQueue::Instance().TakeAll())
            RunOne(request);
    }

    void OnOrderGone(uint32_t botGuidLow)
    {
        m_emptyPasses.erase(botGuidLow);
    }

private:
    // Consecutive bank passes that moved nothing, per bot. A pass that moves
    // anything resets the count; the bank counts as refused after
    // kDeliveryBankRetries empty passes in a row.
    std::unordered_map<uint32_t, uint32_t> m_emptyPasses;

    void RunOne(PendingDelivery const& request)
    {
        std::optional<OrderRecord> stored = Find(request.botGuidLow);
        if (!stored || stored->itemId != request.itemId ||
            stored->state != OrderState::Delivering || !stored->itemId)
        {
            m_emptyPasses.erase(request.botGuidLow);
            return;
        }
        ObjectGuid guid(HIGHGUID_PLAYER, request.botGuidLow);
        Player* bot = sObjectAccessor.FindPlayer(guid);
        if (!bot || !bot->IsInWorld() || bot->GetGuildId() != stored->guildId)
            return;
        // Killed or pulled aggro after queueing: leave the order alone, the
        // map action re-queues once the bot is alive and out of combat.
        if (!bot->IsAlive() || bot->IsInCombat())
            return;
        ::PlayerbotAI* ai = ::PlayerbotAIStorage::Instance().GetAI(bot);
        Unit* banker = FindNearbyGuildBanker(bot, ai);
        bool mailLeg = !banker && DeliveryMailsAfterNoBanker(NoBankerSearches(ai));
        if (!banker && !mailLeg)
        {
            // Left the banker (or the banker despawned): the map action
            // re-queues when the bot stands at one again.
            return;
        }
        Guild* guild = sGuildMgr.GetGuildById(stored->guildId);
        if (!guild)
            return;

        uint32_t& emptyPasses = m_emptyPasses[request.botGuidLow];
        bool mailedAny = stored->reason == OrderReason::NoBank;
        uint32_t delivered = stored->delivered;
        // Anything already banked or mailed reached the guild during this
        // delivery: delivered only rises inside the drain and resets to 0
        // when the delivery leg ends.
        bool movedThisDelivery = delivered > 0;

        uint32_t banked = 0;
        if (!mailLeg)
        {
            banked = DepositPass(bot, guild, banker, stored->itemId, delivered, stored->amount);
            if (banked > 0)
            {
                AddDelivered(request.botGuidLow, banked);
                emptyPasses = 0;
                movedThisDelivery = true;
                delivered += banked;
            }
            else
                ++emptyPasses;
        }

        bool bankDone = delivered >= stored->amount;
        if (!bankDone && (mailLeg || BankRefused(emptyPasses)))
        {
            ObjectGuid receiver = MailReceiverGuid(*stored, bot->getObjectGuid());
            if (receiver)
            {
                bool mailed = false;
                uint32_t count = MailStacks(bot, stored->itemId, receiver, delivered, stored->amount, mailed);
                if (count > 0)
                {
                    AddDelivered(request.botGuidLow, count);
                    movedThisDelivery = true;
                    delivered += count;
                }
                if (mailed)
                    mailedAny = true;
                bankDone = delivered >= stored->amount;
            }
            // Without a receiver nothing further can move: fall through to
            // the stuck classification below instead of retrying forever.
        }

        stored = Find(request.botGuidLow);
        if (!stored || stored->itemId != request.itemId)
        {
            m_emptyPasses.erase(request.botGuidLow);
            return;
        }
        bool finished = DeliveryFinished(emptyPasses, bot->GetItemCount(stored->itemId),
                                         stored->delivered, stored->amount);
        if (!finished)
            return;
        m_emptyPasses.erase(request.botGuidLow);
        DeliveryOutcome outcome =
            ClassifyDelivery(stored->delivered, stored->amount, mailedAny, movedThisDelivery);
        if (DeliveryTimeoutEnds(stored->reason, outcome))
        {
            SetState(request.botGuidLow, OrderState::Cant, stored->reason);
            ExpireTargetFor(ai);
            return;
        }
        switch (outcome)
        {
            case DeliveryOutcome::Done:
                DeliverCraftLeftovers(bot, guild, banker, *stored);
                // A keep order that landed in the bank watches it from now on
                // instead of ending: the store re-arms the next round when
                // the bank drops below the stock target.
                if (KeepWatchesBank(stored->keep, stored->stockTarget))
                    StartKeepWatch(request.botGuidLow);
                else
                    SetState(request.botGuidLow, OrderState::Done, OrderReason::None);
                break;
            case DeliveryOutcome::Mailed:
                DeliverCraftLeftovers(bot, guild, banker, *stored);
                // Mailed means no usable bank: the keep is dropped with the order.
                SetState(request.botGuidLow, OrderState::Mailed, OrderReason::NoBank);
                DropKeep(request.botGuidLow);
                break;
            case DeliveryOutcome::BackToWork:
                SetState(request.botGuidLow, OrderState::Working, OrderReason::None);
                break;
            case DeliveryOutcome::Stuck:
                SetState(request.botGuidLow, OrderState::Cant, OrderReason::Stuck);
                break;
        }
        ExpireTargetFor(ai);
    }
};

class SupplyDrain
{
public:
    static SupplyDrain& Instance()
    {
        static SupplyDrain instance;
        return instance;
    }

    void Update()
    {
        for (PendingSupply const& request : SupplyQueue::Instance().TakeAll())
            RunOne(request);
    }

private:
    void RunOne(PendingSupply const& request)
    {
        std::optional<OrderRecord> order = Find(request.botGuidLow);
        if (!order || order->state != OrderState::Working || order->supplyStep != SupplyStep::Bank ||
            !order->supplyCount)
            return;
        Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, request.botGuidLow));
        if (!bot || !bot->IsInWorld() || bot->GetGuildId() != order->guildId ||
            !bot->IsAlive() || bot->IsInCombat())
            return;
        ::PlayerbotAI* ai = ::PlayerbotAIStorage::Instance().GetAI(bot);
        Unit* banker = FindNearbyGuildBanker(bot, ai);
        Guild* guild = sGuildMgr.GetGuildById(order->guildId);
        if (!banker || !guild || !guild->_Bank)
            return;

        ObjectGuid oldSelection = bot->GetSelectionGuid();
        bot->SetSelectionGuid(banker->getObjectGuid());
        if (order->supplyGoldCount)
        {
            std::unique_ptr<QueryResult> bankMoney(CharacterDatabase.PQuery(
                "SELECT `money` FROM `guild_bank_money` WHERE `guildid` = '%u' AND `isInferno` = '0'",
                order->guildId));
            Field* moneyFields = bankMoney ? bankMoney->Fetch() : nullptr;
            uint32_t bankAvailable = moneyFields && moneyFields[0].GetInt32() > 0 ?
                uint32_t(moneyFields[0].GetInt32()) : 0;
            uint32_t before = bot->GetMoney();
            uint32_t take = std::min(order->supplyGoldCount,
                std::min(bankAvailable, uint32_t(INT32_MAX) - before));
            if (!take)
            {
                SetSupply(request.botGuidLow, order->supplyReturnStep, order->supplyItemId,
                    order->supplyCount);
                bot->SetSelectionGuid(oldSelection);
                return;
            }
            std::ostringstream message;
            message << "WithdrawMoney:" << take;
            guild->_Bank->HandleAddonMessages(message.str(), bot);
            uint32_t after = bot->GetMoney();
            if (after > before)
            {
                uint32_t withdrawn = after - before;
                RecordGuildGoldWithdrawal(request.botGuidLow, withdrawn);
                SetSupply(request.botGuidLow, order->supplyReturnStep, order->supplyItemId,
                    order->supplyCount);
            }
            else
                SetSupply(request.botGuidLow, order->supplyReturnStep, order->supplyItemId,
                    order->supplyCount, SupplyStep::None, OrderReason::BankDenied);
            bot->SetSelectionGuid(oldSelection);
            return;
        }

        uint32_t moved = 0;
        uint32_t remaining = order->supplyCount;
        bool bankHasItem = false;
        bool withdrawAttempted = false;
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(order->supplyItemId);
        uint32_t stackSize = proto ? proto->GetMaxStackSize() : 0;
        if (stackSize)
        {
            for (uint32_t attempt = 0; attempt < 20 && remaining; ++attempt)
            {
                std::unique_ptr<QueryResult> bankItems(CharacterDatabase.PQuery(
                    "SELECT `tab`, `slot`, `count` FROM `guild_bank` WHERE `guildid` = '%u' AND "
                    "`isInferno` = '0' AND `item_template` = '%u' ORDER BY `tab`, `slot`",
                    order->guildId, order->supplyItemId));
                if (!bankItems)
                    break;
                Field* fields = bankItems->Fetch();
                if (!fields)
                    break;
                uint32_t tab = fields[0].GetUInt32();
                uint32_t slot = fields[1].GetUInt32();
                uint32_t bankCount = fields[2].GetUInt32();
                bankHasItem = bankCount > 0;
                uint32_t count = std::min(remaining, std::min(bankCount, stackSize));
                if (!count)
                    break;

                ItemPosCountVec destination;
                if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, order->supplyItemId, count) != EQUIP_ERR_OK ||
                    destination.empty())
                    break;
                uint8_t playerBag = 0, playerSlot = 0;
                if (!ClientInventorySlot(destination.front().pos, playerBag, playerSlot))
                    break;

                uint32_t before = bot->GetItemCount(order->supplyItemId);
                std::ostringstream message;
                message << "WithdrawItem:" << tab << ':' << slot << ':' << uint32_t(playerBag) << ':'
                        << uint32_t(playerSlot) << ':' << count;
                withdrawAttempted = true;
                guild->_Bank->HandleAddonMessages(message.str(), bot);
                uint32_t after = bot->GetItemCount(order->supplyItemId);
                if (after <= before)
                    break;
                uint32_t got = after - before;
                moved += got;
                remaining = got >= remaining ? 0 : remaining - got;
            }
        }
        bot->SetSelectionGuid(oldSelection);
        if (moved > 0)
        {
            SupplyStep nextStep = remaining ? order->supplyReturnStep : SupplyStep::Bags;
            SetSupply(request.botGuidLow, nextStep, order->supplyItemId, remaining);
        }
        else if (bankHasItem && withdrawAttempted)
            SetSupply(request.botGuidLow, order->supplyReturnStep, order->supplyItemId,
                order->supplyCount, SupplyStep::None, OrderReason::BankDenied);
        else if (bankHasItem)
            SetSupply(request.botGuidLow, order->supplyReturnStep, order->supplyItemId,
                order->supplyCount, SupplyStep::None, OrderReason::NoSource);
        else
            SetSupply(request.botGuidLow, order->supplyReturnStep, order->supplyItemId,
                order->supplyCount);
    }
};

} // namespace

void UpdateDeliveries()
{
    DeliveryDrain::Instance().Update();
}

void UpdateSupplyWithdrawals()
{
    SupplyDrain::Instance().Update();
}

void OnDeliveryOrderGone(uint32_t botGuidLow)
{
    DeliveryDrain::Instance().OnOrderGone(botGuidLow);
}

void OnSupplyOrderGone(uint32_t botGuidLow)
{
    SupplyQueue::Instance().Remove(botGuidLow);
}

void ExpireGuildOrderTarget(uint32_t botGuidLow)
{
    Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, botGuidLow));
    if (!bot)
        return;
    ExpireTargetFor(::PlayerbotAIStorage::Instance().GetAI(bot));
}

} // namespace GuildOrders
} // namespace TortoiseBots
