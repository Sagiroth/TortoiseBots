#include "GuildOrderDelivery.h"

#include "GuildOrderDeliveryPolicy.h"
#include "GuildOrderDeliveryQueue.h"
#include "GuildOrderKeepPolicy.h"
#include "GuildOrderStore.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/TravelMgr.h"
#include "PlayerbotAIStorage.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Guild/GuildMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "GuildBank/GuildBank.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Mail/Mail.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"

#include <list>
#include <mutex>
#include <sstream>
#include <unordered_map>

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
                // A keep order that landed in the bank watches it from now on
                // instead of ending: the store re-arms the next round when
                // the bank drops below the stock target.
                if (KeepWatchesBank(stored->keep, stored->stockTarget))
                    StartKeepWatch(request.botGuidLow);
                else
                    SetState(request.botGuidLow, OrderState::Done, OrderReason::None);
                break;
            case DeliveryOutcome::Mailed:
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

} // namespace

void UpdateDeliveries()
{
    DeliveryDrain::Instance().Update();
}

void OnDeliveryOrderGone(uint32_t botGuidLow)
{
    DeliveryDrain::Instance().OnOrderGone(botGuidLow);
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
