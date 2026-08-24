// Forward-ported from mod-playerbots OpenItemAction.cpp - modern donor
// Source: mod-playerbots@5397110, Shyalya@1f9497e Tortoise 1.18.1 baseline
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "OpenItemAction.h"
#include "AiObjectContext.h"
#include "Objects/ItemPrototype.h"
#include "LootObjectStack.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "WorldPacket.h"

bool OpenItemAction::Execute(Event& /*event*/)
{
    bool foundOpenable = false;

    Item* item = nullptr;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END && !item; ++slot)
    {
        Item* candidate = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (candidate && candidate->GetProto() &&
            ((candidate->GetProto()->Flags & ITEM_FLAG_LOOTABLE) || candidate->GetProto()->LockID))
            item = candidate;
    }
    if (item)
    {
        uint8 bag = item->GetBagSlot();  // Retrieves the bag slot (0 for main inventory)
        uint8 slot = item->GetSlot();    // Retrieves the actual slot inside the bag

        OpenItem(item, bag, slot);
        foundOpenable = true;
    }

    return foundOpenable;
}

void OpenItemAction::OpenItem(Item* item, uint8 bag, uint8 slot)
{
    WorldPacket packet(CMSG_OPEN_ITEM);
    packet << bag << slot;
    bot->GetSession()->HandleOpenItemOpcode(packet);

    // Store the item GUID as the loot target
    LootObject lootObject;
    lootObject.guid = item->GetGUID();
    botAI->GetAiObjectContext()->GetValue<LootObject>("loot target")->Set(lootObject);

    std::ostringstream out;
    out << "Opened item: " << item->GetProto()->Name1;
    botAI->TellPlayerNoFacing(botAI->GetMaster(), out.str());
}
