
#include "playerbot/playerbot.h"
#include "GuildBankAction.h"

#include "GuildBank/GuildBank.h"
#include "Mail/Mail.h"
#include "playerbot/strategy/ItemVisitors.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "runtime/GuildOrderDeliveryPolicy.h"
#include "runtime/GuildOrderStore.h"

using namespace ai;
using namespace TortoiseBots::GuildOrders;

namespace
{
    // Entries the core accepts as guild bankers outright; anything else must
    // carry the guild-banker gossip option (same list as the bank itself).
    bool IsGuildBankerEntry(uint32 entry)
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

    // Client numbering the bank message expects: backpack 0 with slots from 1,
    // other bags 1-4 with slots from 1. Returns false for anything worn.
    bool ClientBagSlot(Item* item, uint8& bag, uint8& slot)
    {
        uint8 serverBag = item->GetBagSlot();
        uint8 serverSlot = item->GetSlot();
        if (serverBag == INVENTORY_SLOT_BAG_0)
        {
            if (serverSlot < INVENTORY_SLOT_ITEM_START || serverSlot >= INVENTORY_SLOT_ITEM_END)
                return false;
            bag = 0;
            slot = uint8(serverSlot - INVENTORY_SLOT_ITEM_START + 1);
            return true;
        }
        if (serverBag < INVENTORY_SLOT_BAG_START || serverBag >= INVENTORY_SLOT_BAG_END)
            return false;
        bag = uint8(serverBag - INVENTORY_SLOT_BAG_START + 1);
        slot = uint8(serverSlot + 1);
        return true;
    }
}

bool GuildBankAction::isUseful()
{
    if (!bot->GetGuildId())
        return false;
    if (bot->IsInCombat() || bot->IsDead())
        return false;
    if (ai->HasRealPlayerMaster())
        return false;
    std::optional<OrderRecord> stored = TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!stored || stored->guildId != bot->GetGuildId())
        return false;
    if (stored->state != OrderState::Delivering || !stored->itemId)
        return false;
    return bot->GetItemCount(stored->itemId) > 0;
}

Unit* GuildBankAction::FindNearbyGuildBanker()
{
    std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
    for (ObjectGuid& guid : npcs)
    {
        Unit* npc = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
        if (npc && npc->IsCreature() && IsGuildBanker(npc->ToCreature()))
            return npc;
    }
    return nullptr;
}

bool GuildBankAction::DepositAtBanker(Unit* banker, uint32 itemId)
{
    if (!banker)
        return false;
    Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
    if (!guild || !guild->_Bank)
        return false;

    ObjectGuid oldSelection = bot->GetSelectionGuid();
    bot->SetSelectionGuid(banker->getObjectGuid());

    uint32 deliveredNow = 0;
    for (size_t tab = 0; tab < kDeliveryTabCount && bot->GetItemCount(itemId) > 0; ++tab)
    {
        uint32 beforeTab = bot->GetItemCount(itemId);
        uint32 retries = 0;
        for (;;)
        {
            FindItemByIdVisitor visitor(itemId);
            ai->InventoryIterateItems(&visitor,
                IterateItemsMask(uint8(IterateItemsMask::ITERATE_ITEMS_IN_BAGS)));
            std::list<Item*> stacks = visitor.GetResult();
            if (stacks.empty())
                break;
            uint32 before = bot->GetItemCount(itemId);
            for (Item* stack : stacks)
            {
                uint8 bag = 0, slot = 0;
                if (!ClientBagSlot(stack, bag, slot))
                    continue;
                std::ostringstream msg;
                msg << "DepositItem:" << uint32(bag) << ':' << uint32(slot) << ':'
                    << uint32(DeliveryTabAt(tab)) << ":0:" << stack->GetCount();
                guild->_Bank->HandleAddonMessages(msg.str(), bot);
            }
            uint32 after = bot->GetItemCount(itemId);
            if (after >= before)
            {
                // Nothing moved: either the tab refused us (no rights, full,
                // no bank at all) or a save is running. The save only lasts
                // a moment, so retry a few times before moving to the next tab.
                if (++retries > kDeliveryBankRetries)
                    break;
                continue;
            }
            deliveredNow += before - after;
            retries = 0;
        }
        if (beforeTab == bot->GetItemCount(itemId))
            continue;
        if (bot->GetItemCount(itemId) == 0)
            break;
    }

    bot->SetSelectionGuid(oldSelection);

    if (deliveredNow > 0)
    {
        TortoiseBots::GuildOrders::AddDelivered(bot->GetGUIDLow(), deliveredNow);
        return true;
    }
    return false;
}

void GuildBankAction::MailToLeader(uint32 itemId)
{
    Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
    if (!guild)
        return;
    ObjectGuid leader = guild->GetLeaderGuid();
    if (!leader || leader == bot->getObjectGuid())
        return;

    FindItemByIdVisitor visitor(itemId);
    ai->InventoryIterateItems(&visitor,
        IterateItemsMask(uint8(IterateItemsMask::ITERATE_ITEMS_IN_BAGS)));
    std::list<Item*> stacks = visitor.GetResult();
    if (stacks.empty())
        return;

    Player* receiver = sObjectMgr.GetPlayer(leader);
    std::string leaderName = receiver ? receiver->GetName() : "";
    if (leaderName.empty())
        sObjectMgr.GetPlayerNameByGUID(leader, leaderName);

    uint32 mailedNow = 0;
    for (Item* stack : stacks)
    {
        std::ostringstream body;
        body << "Hello,\n\nHere are the ordered items your guild bank could not take.\n\n"
             << bot->GetName() << '\n';
        MailDraft draft("Guild order delivery", body.str());
        uint32 count = stack->GetCount();
        bot->MoveItemFromInventory(stack->GetBagSlot(), stack->GetSlot(), true);
        stack->DeleteFromInventoryDB();
        stack->SetOwnerGuid(leader);
        stack->SaveToDB();
        draft.AddItem(stack);
        draft.SendMailTo(MailReceiver(receiver, leader), MailSender(bot));
        mailedNow += count;
    }

    if (mailedNow > 0)
        TortoiseBots::GuildOrders::AddDelivered(bot->GetGUIDLow(), mailedNow);

    std::ostringstream out;
    out << "Mailed " << mailedNow << "x ordered items to " << (leaderName.empty() ? "the guild master" : leaderName);
    ai->TellDebug(ai->GetMaster(), out.str(), "debug travel");
}

bool GuildBankAction::Execute(Event& event)
{
    (void)event;
    std::optional<OrderRecord> stored = TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!stored || stored->guildId != bot->GetGuildId() || stored->state != OrderState::Delivering)
        return false;
    uint32 itemId = stored->itemId;
    if (!itemId || bot->GetItemCount(itemId) == 0)
        return false;

    bool mailedAny = stored->reason == OrderReason::NoBank;
    if (Unit* banker = FindNearbyGuildBanker())
        DepositAtBanker(banker, itemId);

    if (bot->GetItemCount(itemId) > 0)
    {
        // Still carrying order items after the bank: refused on every tab
        // (or no bank at all), so mail them to the guild master.
        MailToLeader(itemId);
        mailedAny = true;
    }

    stored = TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!stored || stored->itemId != itemId)
        return true;
    DeliveryOutcome outcome = ClassifyDelivery(stored->delivered, stored->amount, mailedAny);
    switch (outcome)
    {
        case DeliveryOutcome::Done:
            TortoiseBots::GuildOrders::SetState(bot->GetGUIDLow(), OrderState::Done, OrderReason::None);
            break;
        case DeliveryOutcome::Mailed:
            TortoiseBots::GuildOrders::SetState(bot->GetGUIDLow(), OrderState::Mailed, OrderReason::NoBank);
            break;
        case DeliveryOutcome::BackToWork:
            TortoiseBots::GuildOrders::SetState(bot->GetGUIDLow(), OrderState::Working, OrderReason::None);
            break;
    }
    return true;
}
