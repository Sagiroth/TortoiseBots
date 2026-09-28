
#include "playerbot/playerbot.h"
#include "EquipAction.h"

#include <set>

#include "playerbot/RandomItemMgr.h"
#include "playerbot/strategy/values/ItemCountValue.h"
#include "playerbot/strategy/values/ItemUsageValue.h"

using namespace ai;

bool EquipAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();
    if (text == "?")
    {
        ListItems(requester);
        return true;
    }

    uint8 targetSlot = NULL_SLOT;
    if (text.find("mh ") == 0)
    {
        targetSlot = EQUIPMENT_SLOT_MAINHAND;
        text = text.substr(3);
    }
    else if (text.find("oh ") == 0)
    {
        targetSlot = EQUIPMENT_SLOT_OFFHAND;
        text = text.substr(3);
    }

    ItemIds ids = chat->parseItems(text);
    if (ids.empty())
    {
        //Get items based on text.
        std::list<Item*> found = ai->InventoryParseItems(text, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);

        //Sort items on itemLevel descending.
        found.sort([](Item* i, Item* j) {return i->GetProto()->ItemLevel > j->GetProto()->ItemLevel; });

        std::vector< uint16> dests;
        for (auto& item : found)
        {
            uint32 itemId = item->GetProto()->ItemId;
            if (std::find(ids.begin(), ids.end(), itemId) != ids.end())
            {
                continue;
            }

            uint16 dest;
            InventoryResult msg = bot->CanEquipItem(targetSlot, dest, item, true);

            if (msg != EQUIP_ERR_OK)
            {
                continue;
            }

            if (std::find(dests.begin(), dests.end(), dest) != dests.end())
            {
                continue;
            }

            dests.push_back(dest);
            ids.insert(itemId);
        }
    }

    if (targetSlot != NULL_SLOT)
    {
        EquipItemsToSlot(requester, ids, targetSlot);
    }
    else
    {
        EquipItems(requester, ids);
    }
    return true;
}

void EquipAction::ListItems(Player* requester)
{
    ai->TellPlayer(requester, "=== Equip ===");

    std::map<uint32, int> items;
    std::map<uint32, bool> soulbound;
    for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (pItem)
            {
                items[pItem->GetProto()->ItemId] += pItem->GetCount();
            }
        }
    }

    ai->InventoryTellItems(requester, items, soulbound);
}

void EquipAction::EquipItems(Player* requester, ItemIds ids)
{
    for (ItemIds::iterator i = ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        EquipItem(requester, &visitor);
    }
}

void EquipAction::EquipItemsToSlot(Player* requester, ItemIds ids, uint8 targetSlot)
{
    for (ItemIds::iterator i = ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        std::list<Item*> items = visitor.GetResult();
        if (!items.empty())
        {
            EquipItemToSlot(requester, *items.begin(), targetSlot);
        }
    }
}

void EquipAction::EquipItem(Player* requester, FindItemVisitor* visitor)
{
    ai->InventoryIterateItems(visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    std::list<Item*> items = visitor->GetResult();
	if (!items.empty())
    {
        EquipItem(ai, requester, *items.begin());
    }
}

//Return the bag slot with smallest bag
uint8 EquipAction::GetSmallestBagSlot(Player* bot)
{
    int8 curBag = 0;
    uint32 curSlots = 0;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        const Bag* const pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (pBag)
        {
            if (curBag > 0 && curSlots < pBag->GetBagSize())
            {
                continue;
            }

            curBag = bag;
            curSlots = pBag->GetBagSize();
        }
        else
        {
            return bag;
        }
    }

    return curBag;
}

void EquipAction::EquipItemToSlot(Player* requester, Item* item, uint8 targetSlot)
{
    uint8 bagIndex = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    uint32 itemId = item->GetProto()->ItemId;

    uint16 dest;
    InventoryResult msg = bot->CanEquipItem(targetSlot, dest, item, true);
    if (msg != EQUIP_ERR_OK)
    {
        bot->SendEquipError(msg, item, nullptr);
        return;
    }

    uint8 destSlot = dest & 0xFF;
    if (destSlot != targetSlot)
    {
        ai->TellPlayer(requester, "Cannot equip this item to the specified slot.");
        return;
    }

    Item* oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, targetSlot);
    Item* oldOffhand = nullptr;
    if (destSlot == EQUIPMENT_SLOT_MAINHAND && item->GetProto()->InventoryType == INVTYPE_2HWEAPON)
        oldOffhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);

    uint16 src = ((bagIndex << 8) | slot);
    uint16 dstPos = ((INVENTORY_SLOT_BAG_0 << 8) | targetSlot);

    bot->SwapItem(src, dstPos);

    RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());

    if (oldItem)
        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldItem).GetQualifier());
    if (oldOffhand)
        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldOffhand).GetQualifier());

    sPlayerbotAIConfig.logEvent(ai, "EquipAction", item->GetProto()->Name1, std::to_string(item->GetProto()->ItemId));

    std::map<std::string, std::string> args;
    args["%item"] = chat->formatItem(item);
    ai->TellPlayer(requester, BOT_TEXT2("equip_command", args), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
}

void EquipAction::EquipItem(PlayerbotAI* ai, Player* requester, Item* item, bool silent)
{
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();

    uint8 bagIndex = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    uint32 itemId = item->GetProto()->ItemId;
    ItemPrototype const* newProto = item->GetProto();

    uint16 dest;
    InventoryResult result = bot->CanEquipItem(NULL_SLOT, dest, item, !item->IsBag());

    Item* oldItem = nullptr;
    Item* oldOffhand = nullptr;
    if (result == EQUIP_ERR_OK)
    {
        oldItem = bot->GetItemByPos(dest);

        if (oldItem && oldItem->GetSlot() == EQUIPMENT_SLOT_MAINHAND && item->GetProto()->InventoryType == INVTYPE_2HWEAPON)
            oldOffhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    }

    if (item->GetProto()->InventoryType == INVTYPE_AMMO)
    {
        bot->SetAmmo(itemId);
    }
    else
    {
        bool equipedBag = false;
        bool isBag = item->GetProto()->Class == ITEM_CLASS_CONTAINER || item->GetProto()->Class == ITEM_CLASS_QUIVER;
        if (isBag)
        {
            uint8 newBagSlot = GetSmallestBagSlot(bot);

            // Only replace the smallest equipped bag when it is empty. The
            // core rejects moving a non-empty bag from a specific slot.
            Item* const oldBag = newBagSlot > 0 ? bot->GetItemByPos(INVENTORY_SLOT_BAG_0, newBagSlot) : nullptr;
            const bool oldBagIsFull = oldBag && oldBag->IsBag() && !((Bag*)oldBag)->IsEmpty();

            if (newBagSlot > 0 && !oldBagIsFull)
            {
                uint16 src = ((bagIndex << 8) | slot);

                if (newBagSlot == item->GetBagSlot()) // The new bag is in the target slot. Move it to the pack first.
                {
                    uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | INVENTORY_SLOT_ITEM_START);
                    bot->SwapItem(src, dst);
                    src = dst;
                }

                uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | newBagSlot);
                bot->SwapItem(src, dst);
                equipedBag = true;
            }
        }

        if (!equipedBag && !isBag)
        {
            WorldPacket packet(CMSG_AUTOEQUIP_ITEM, 2);
            packet << bagIndex << slot;
            bot->GetSession()->HandleAutoEquipItemOpcode(packet);
        }

        // A hunter swapping bow<->gun keeps the stale ammo id: after the
        // equip completes, re-equip matching ammo from bags (Issue #219).
        if (newProto && (newProto->Class == ITEM_CLASS_WEAPON) &&
            (bot->GetClass() == CLASS_HUNTER || bot->GetClass() == CLASS_ROGUE || bot->GetClass() == CLASS_WARRIOR))
        {
            Item* ranged = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
            if (ranged && ranged->GetProto() && ranged->GetProto()->ItemId == itemId)
                ai->DoSpecificAction("equip ammo", Event(), true);
        }
    }

    RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());

    if (oldItem)
        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldItem).GetQualifier());
    if (oldOffhand)
        RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oldOffhand).GetQualifier());

    sPlayerbotAIConfig.logEvent(ai, "EquipAction", item->GetProto()->Name1, std::to_string(item->GetProto()->ItemId));

    if (!silent)
    {
        std::map<std::string, std::string> args;
        args["%item"] = ChatHelper::formatItem(item);
        ai->TellPlayer(requester, BOT_TEXT2("equip_command", args), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }
}

bool EquipUpgradesAction::Execute(Event& event)
{
    if (!sPlayerbotAIConfig.autoEquipUpgradeLoot && !sRandomBotFacade.IsRandomBot(bot))
        return false;

    if (event.GetSource() == "trade status")
    {
        WorldPacket p(event.GetPacket());
        p.rpos(0);
        uint32 status;
        p >> status;

        if (status != TRADE_STATUS_TRADE_ACCEPT)
        {
            return false;
        }
    }
    else if (event.GetSource() == "item push result")
    {
        bool valid = false;
        WorldPacket& data = event.GetPacket();
        if (!data.empty())
        {
            data.rpos(0);

            ObjectGuid guid;
            data >> guid;
            if (guid != bot->getObjectGuid())
            {
                return false;
            }

            uint32 received, created, isShowChatMessage, slotId, itemId, suffixFactor, count;
            uint32 itemRandomPropertyId;
            //uint32 invCount;
            uint8 bagSlot;

            data >> received;                               // 0=looted, 1=from npc
            data >> created;                                // 0=received, 1=created
            data >> isShowChatMessage;                                      // IsShowChatMessage
            data >> bagSlot;
            // item slot, but when added to stack: 0xFFFFFFFF
            data >> slotId;
            data >> itemId;
            data >> suffixFactor;
            data >> itemRandomPropertyId;
            data >> count;
            // data >> invCount; // [-ZERO] count of items in inventory

            ItemQualifier itemQualifier(itemId, (int32)itemRandomPropertyId);
            const ItemPrototype* itemProto = itemQualifier.GetProto();
            if (itemProto && (itemProto->Class == ItemClass::ITEM_CLASS_WEAPON ||
                              itemProto->Class == ItemClass::ITEM_CLASS_ARMOR ||
                              itemProto->Class == ItemClass::ITEM_CLASS_CONTAINER))
            {
                valid = true;
            }
        }

        if (!valid)
        {
            return false;
        }
    }

    // QueryItemUsageForEquip compares a bag item against whatever is in its
    // destination slot, so the equipped weapons must stay where they are.
    // Unequipping MH/OH first (donor lineage, present since the 204-file
    // forward-port 450365a) made every bag weapon read as an empty-slot
    // upgrade, left the spec-transition comparison in ItemUsageValue with no
    // old weapon to look at, and cost two packet round-trips plus a visible
    // flicker and swing-timer reset on every run. Evaluate against live
    // equipment instead.
    context->ClearExpiredValues("item usage", 10); //Clear old item usage.

    std::list<Item*> items;

    FindItemUsageVisitor visitor(bot, ItemUsage::ITEM_USAGE_EQUIP);
    ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    visitor.SetUsage(ItemUsage::ITEM_USAGE_BAD_EQUIP);
    ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    items = visitor.GetResult();

    // EQUIP (a real upgrade over what is equipped) before BAD_EQUIP (fills an
    // empty slot), then by stat weight: a heavier off-spec weapon must not
    // claim a slot ahead of the spec weapon that belongs there. The
    // mainhand-first tiebreaker only decides between two equal-weight EQUIP
    // weapons.
    PlayerbotAI* sortAi = ai;
    AiObjectContext* sortContext = sortAi->GetAiObjectContext();
    items.sort([sortContext, plr = bot](Item* i, Item* j) {
        ItemUsage iUsage = sortContext->GetValue<ItemUsage>("item usage", ItemQualifier(i).GetQualifier())->Get();
        ItemUsage jUsage = sortContext->GetValue<ItemUsage>("item usage", ItemQualifier(j).GetQualifier())->Get();
        if (iUsage != jUsage)
            return iUsage < jUsage;

        uint32 iWeight = sRandomItemMgr.ItemStatWeight(plr, i);
        uint32 jWeight = sRandomItemMgr.ItemStatWeight(plr, j);
        if (iWeight != jWeight)
            return iWeight > jWeight;

        bool iMain = i->GetProto()->InventoryType == INVTYPE_WEAPONMAINHAND;
        bool jMain = j->GetProto()->InventoryType == INVTYPE_WEAPONMAINHAND;

        if (iMain != jMain)
            return iMain; // mainhand comes first

        if (i->GetProto()->ItemLevel != j->GetProto()->ItemLevel)
            return i->GetProto()->ItemLevel > j->GetProto()->ItemLevel;

        return i->GetProto()->ItemId < j->GetProto()->ItemId; });

    bool didEquip = false;
    std::set<uint8> filledSlots;

    for (auto& item : items)
    {
        // Re-score against the equipment as it stands now: the visitor scored
        // every candidate before the loop ran, so after equipping one item a
        // later candidate would still carry the usage it got for the slot it
        // would have taken then. Re-resolving the slot below can point it at a
        // different, stronger slot (Ring A took the weak finger, Ring B now
        // resolves to the strong one), and a stale EQUIP would overwrite and
        // downgrade it. Reset forces Calculate() to run again this tick.
        ItemQualifier qualifier(item);
        RESET_AI_VALUE2(ItemUsage, "item usage", qualifier.GetQualifier());
        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", qualifier.GetQualifier());
        if (usage != ItemUsage::ITEM_USAGE_EQUIP && usage != ItemUsage::ITEM_USAGE_BAD_EQUIP)
        {
            continue;
        }

        // Compare against (and equip into) the weaker of the item's eligible
        // slots: core only ever reports the primary one once a ring/trinket
        // pair or a dual-wield hand is full. Re-resolve every iteration, since
        // equipping one item moves the slot the next candidate would land in
        // (a 2H weapon frees the off hand, the first of two rings takes the
        // other finger slot).
        uint8 slot = ItemUsageValue::GetPreferredEquipSlot(bot, item, item->GetProto());
        if (slot == NULL_SLOT || filledSlots.count(slot))
        {
            continue; // no eligible slot, or one item per target slot per run
        }

        uint16 dest;
        if (bot->CanEquipItem(slot, dest, item, true) != EQUIP_ERR_OK || (dest & 0xFF) != slot)
        {
            continue;
        }

        // BAD_EQUIP means "fills an empty slot", so it must never swap out
        // something already equipped; otherwise the next audit trades the
        // two items back and the bot ping-pongs.
        if (usage == ItemUsage::ITEM_USAGE_BAD_EQUIP && bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            continue;
        }

        filledSlots.insert(slot);
        didEquip = true;

        sLog.outDetail("Bot #%d <%s> auto equips item %d (%s)", bot->GetGUIDLow(), bot->GetName(), item->GetProto()->ItemId, usage == ItemUsage::ITEM_USAGE_EQUIP ? "better than current" : "wrong item but empty slot");
        ai->TellDebug(ai->GetMaster(), "Equipping: " + chat->formatItem(item) + " - " + ItemUsageValue::ReasonForNeed(usage, item, 1, bot), "debug equip");

        // Use the auto-equip packet when core would pick the same slot, the
        // explicit-slot packet when the chosen slot is the weaker secondary
        // one that FindEquipSlot(NULL_SLOT) never returns.
        uint16 defaultDest;
        if (bot->CanEquipItem(NULL_SLOT, defaultDest, item, true) == EQUIP_ERR_OK && (defaultDest & 0xFF) == slot)
        {
            EquipItem(ai, GetMaster(), item);
        }
        else
        {
            EquipItemToSlot(GetMaster(), item, slot);
        }
    }

    // Put the higher top-end damage weapon in the main hand. Only when core
    // can actually swap the two (neither weapon is locked to its own hand) and
    // only when both are weapons the bot's spec may wield. GetPlayerSpecId()
    // resolves the class-default scale id whenever no talents are spent, so
    // for any normally provisioned bot this is the strict spec check; the
    // !hasRealSpec escape only applies when the weight-scale table is absent
    // and no fallback can be resolved either (scoring is unavailable then).
    if (didEquip && bot->CanDualWield())
    {
        Item* mh = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        Item* oh = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);

        if (mh && oh
            && mh->GetProto()->Class == ITEM_CLASS_WEAPON
            && oh->GetProto()->Class == ITEM_CLASS_WEAPON
            && mh->GetProto()->InventoryType != INVTYPE_2HWEAPON
            && mh->GetProto()->InventoryType != INVTYPE_WEAPONMAINHAND
            && oh->GetProto()->InventoryType != INVTYPE_WEAPONOFFHAND)
        {
            uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);
            bool const hasRealSpec = (specId != 0);

            if (!hasRealSpec
                || (sRandomItemMgr.ShouldEquipWeaponForSpec(bot->GetClass(), specId, mh->GetProto())
                    && sRandomItemMgr.ShouldEquipWeaponForSpec(bot->GetClass(), specId, oh->GetProto())))
            {
                float mhMaxDmg = mh->GetProto()->Damage[0].DamageMax;
                float ohMaxDmg = oh->GetProto()->Damage[0].DamageMax;

                if (ohMaxDmg > mhMaxDmg)
                {
                    uint16 destMH, destOH;
                    if (bot->CanEquipItem(EQUIPMENT_SLOT_MAINHAND, destMH, oh, true) == EQUIP_ERR_OK
                        && bot->CanEquipItem(EQUIPMENT_SLOT_OFFHAND, destOH, mh, true) == EQUIP_ERR_OK)
                    {
                        uint16 srcPos = ((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_MAINHAND);
                        uint16 dstPos = ((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_OFFHAND);
                        bot->SwapItem(srcPos, dstPos);

                        // Only claim success when the swap really happened.
                        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND) == oh)
                        {
                            RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(oh).GetQualifier());
                            RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(mh).GetQualifier());

                            sLog.outDetail("Bot #%d <%s> swapped MH/OH weapons to put higher top-end damage (%.1f) in main hand",
                                bot->GetGUIDLow(), bot->GetName(), ohMaxDmg);
                        }
                    }
                }
            }
        }
    }

    return didEquip;
}
