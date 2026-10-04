
#include "playerbot/playerbot.h"
#include "DestroyItemAction.h"
#include "playerbot/strategy/values/ItemCountValue.h"
#include "playerbot/QuestLogPolicy.h"
#include "playerbot/strategy/values/MaintenanceValues.h"

using namespace ai;

bool DestroyItemAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();
    ItemIds ids = chat->parseItems(text);

    for (ItemIds::iterator i =ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        DestroyItem(&visitor, requester);
    }

    return true;
}

void DestroyItemAction::DestroyItem(FindItemVisitor* visitor, Player* requester)
{
    ai->InventoryIterateItems(visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    std::list<Item*> items = visitor->GetResult();
	for (std::list<Item*>::iterator i = items.begin(); i != items.end(); ++i)
    {
		Item* item = *i;
        std::ostringstream out; out << chat->formatItem(item) << " destroyed";
        bot->DestroyItem(item->GetBagSlot(),item->GetSlot(), true);
        ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }
}

bool SmartDestroyItemAction::DestroyGreyJunk(Player* requester)
{
    //Item ids, never item pointers: DestroyItem() frees every stack carrying
    //the id, so a second stack of an id already thrown away would be a
    //dangling pointer. Stacks of one id are merged while reading, then the
    //ids are ordered by value so the cheapest grey goes first - a map alone
    //would only order them by item id.
    std::map<uint32, uint32> valueById;
    for (Item* item : AI_VALUE2(std::list<Item*>, "inventory items", "usage " + std::to_string((uint8)ItemUsage::ITEM_USAGE_VENDOR)))
    {
        ItemPrototype const* proto = item->GetProto();

        if (proto && proto->Quality == ITEM_QUALITY_POOR)
            valueById[proto->ItemId] += proto->SellPrice * item->GetCount();
    }

    std::list<std::pair<uint32, uint32>> cheapestFirst(valueById.begin(), valueById.end());
    cheapestFirst.sort([](std::pair<uint32, uint32> const& left, std::pair<uint32, uint32> const& right) { return left.second < right.second; });

    for (auto& grey : cheapestFirst)
    {
        if (HAS_AI_VALUE2("force item usage", grey.first))
            continue;

        FindItemByIdVisitor visitor(grey.first);
        DestroyItem(&visitor, requester);

        if (AI_VALUE(uint8, "bag space") < 90)
            return true;
    }

    return false;
}

//Destroys the listed usages in order, newest item of each usage first, and
//stops as soon as the bags are back under the 90% threshold. Returns true
//when that happened.
bool SmartDestroyItemAction::DestroyUsages(Player* requester, std::vector<ItemUsage> const& usages)
{
    for (auto& usage : usages)
    {
        std::list<uint32> items = AI_VALUE2(std::list<uint32>, "inventory item ids", "usage " + std::to_string((uint8)usage));

        items.reverse();

        for (auto& item : items)
        {
            if (HAS_AI_VALUE2("force item usage", item))
                continue;

            FindItemByIdVisitor visitor(item);
            DestroyItem(&visitor, requester);

            if (AI_VALUE(uint8, "bag space") < 90)
                return true;
        }
    }

    return false;
}

bool SmartDestroyItemAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    uint8 bagSpace = AI_VALUE(uint8, "bag space");

    // Bone Chew Toy (item 51751): the only quest needing it is the inactive
    // 40298, so copies already in bags are junk. Destroyed first, before
    // the grey-only branch below (the toy is white quality and KEEP usage
    // never reaches the destroy lists on its own). Keyed off the item id -
    // never a generic quest-class purge, which would also eat quest
    // starters (Free Ticket Voucher 19338 etc.).
    if (!ai->HasActivePlayerMaster())
    {
        FindItemByIdVisitor chewToy(ai::kBoneChewToyItemId);
        ai->InventoryIterateItems(&chewToy, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        if (!chewToy.GetResult().empty())
        {
            DestroyItem(&chewToy, requester);
            return true;
        }
    }

    bool onlyDestroyGray = false;

    if (ai->HasRealPlayerMaster() || ai->IsInRealGuild())
        onlyDestroyGray = true;

    if(sPlayerbotAIConfig.IsFreeAltBot(bot) && !ai->HasActivePlayerMaster())
        onlyDestroyGray = false;

    // only destroy grey items if with real player/guild
    if (onlyDestroyGray)
    {
        //Item ids, not item pointers: destroying one id frees every stack of
        //it, so a later stack of the same id would already be dangling.
        std::set<uint32> itemIds;
        FindItemsToTradeByQualityVisitor visitor(ITEM_QUALITY_POOR, 5);
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        for (Item* item : visitor.GetResult())
            itemIds.insert(item->GetProto()->ItemId);

        for (auto& itemId : itemIds)
        {
            if (HAS_AI_VALUE2("force item usage", itemId))
                continue;

            FindItemByIdVisitor visitor(itemId);
            DestroyItem(&visitor, requester);

            bagSpace = AI_VALUE(uint8, "bag space");

            if (bagSpace < 90)
                return true;
        }

        return true;
    }

    //We need money: keep quest items and anything a vendor would buy (sell
    //first), destroy only genuinely useless stuff.
    bool const needsMoney = AI_VALUE(bool, "should get money") || ShouldSellValue::CantAffordNextSpell(ai);

    std::vector<ItemUsage> bestToDestroy = { ItemUsage::ITEM_USAGE_NONE }; //First destroy anything useless.

    if (needsMoney)
    {
        if (!AI_VALUE(bool, "can sell")) //Quest items can't directly be sold.
            bestToDestroy.push_back(ItemUsage::ITEM_USAGE_QUEST);
    }
    else //We don't need money so destroy the cheapest stuff.
    {
        bestToDestroy.push_back(ItemUsage::ITEM_USAGE_VENDOR);
        bestToDestroy.push_back(ItemUsage::ITEM_USAGE_BROKEN_AH);
        bestToDestroy.push_back(ItemUsage::ITEM_USAGE_AH);
    }

    if (DestroyUsages(requester, bestToDestroy))
        return true;

    //A full bag must never cost the bot its potions and food to protect grey
    //trash worth a few copper, so grey goes before profession stock and
    //consumables - cheapest first, keeping what is worth selling.
    if (needsMoney && DestroyGreyJunk(requester))
        return true;

    //If we still need room
    return DestroyUsages(requester, { ItemUsage::ITEM_USAGE_SKILL, ItemUsage::ITEM_USAGE_USE }); //Items that might help tradeskill are more important than above but still expendable. These are more likely to be useful 'soon' but still expendable.
}

bool DestroyAllGrayItemsAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();

    bool hasDestroyedAnyItems = false;

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                {
                    if (const ItemPrototype* proto = pItem->GetProto())
                    {
                        if (proto->Quality == ITEM_QUALITY_POOR)
                        {
                            std::ostringstream out; out << ai->GetChatHelper()->formatItem(pItem->GetProto()) << " destroyed";
                            sLog.outDetail("%s via DestroyAllGrayItemsAction", out.str().c_str());
                            bot->DestroyItem(pItem->GetBagSlot(), pItem->GetSlot(), true);
                            ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

                            hasDestroyedAnyItems = true;
                        }
                    }
                }
            }
        }
    }

    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (const ItemPrototype* proto = pItem->GetProto())
            {
                if (proto->Quality == ITEM_QUALITY_POOR)
                {
                    std::ostringstream out; out << ai->GetChatHelper()->formatItem(pItem->GetProto()) << " destroyed";
                    sLog.outDetail("%s via DestroyAllGrayItemsAction", out.str().c_str());
                    bot->DestroyItem(pItem->GetBagSlot(), pItem->GetSlot(), true);
                    ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

                    hasDestroyedAnyItems = true;
                }
            }
        }
    }

    return hasDestroyedAnyItems;
}