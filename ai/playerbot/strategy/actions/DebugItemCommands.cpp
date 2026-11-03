#include "playerbot/playerbot.h"
#include "DebugAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include <playerbot/TravelNode.h>
#include "ChooseTravelTargetAction.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "playerbot/strategy/actions/RpgSubActions.h"
#include "playerbot/LootObjectStack.h"
#include "GameEventMgr.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/PlayerbotHelpMgr.h"
#include "Transports/Transport.h"
#include "Maps/PathFinder.h"

#include "vmap/VMapFactory.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

#include <iomanip>
#include <typeindex>
#include "Maps/MoveMap.h"

using namespace ai;
using namespace MaNGOS;

bool DebugAction::HandleItem(Event& event, Player* requester, const std::string& text)
{
    std::ostringstream out;

    if (text.size() < 4)
        return false;

    GuidPosition guidP;

    std::string link = text.substr(3);

    if (link.empty())
        return false;

        ItemIds items = chat->parseItems(link);
        if (items.empty())
            return false;

    uint32 itemId = *items.begin();

    Item* item = FindItemByEntryCompat(bot, itemId);

    ItemQualifier qualifier;
    ItemPrototype const* proto;
    if (item)
    {
        qualifier = ItemQualifier(item);
        proto = item->GetProto();
    }
    else
    {
        qualifier = ItemQualifier(itemId);
        proto = qualifier.GetProto();
    }


    if (item)
        out << chat->formatItem(item, item->GetCount());
    else
        out << chat->formatItem(qualifier);

    out << " (id:" << itemId;

    out << ",reqlev:" << proto->RequiredLevel << " ilev:" << proto->ItemLevel;

    out << ") ";


    ai->TellPlayerNoFacing(requester, out);

    std::ostringstream out2;

    // Print class and subclass string equivalents
    static const std::unordered_map<uint32, std::string> itemClassNames = {
        { ITEM_CLASS_CONSUMABLE, "Consumable" },
        { ITEM_CLASS_CONTAINER, "Container" },
        { ITEM_CLASS_WEAPON, "Weapon" },
        { ITEM_CLASS_GEM, "Gem" },
        { ITEM_CLASS_ARMOR, "Armor" },
        { ITEM_CLASS_REAGENT, "Reagent" },
        { ITEM_CLASS_PROJECTILE, "Projectile" },
        { ITEM_CLASS_TRADE_GOODS, "Trade Goods" },
        { ITEM_CLASS_GENERIC, "Generic" },
        { ITEM_CLASS_RECIPE, "Recipe" },
        { ITEM_CLASS_MONEY, "Money" },
        { ITEM_CLASS_QUIVER, "Quiver" },
        { ITEM_CLASS_QUEST, "Quest" },
        { ITEM_CLASS_KEY, "Key" },
        { ITEM_CLASS_PERMANENT, "Permanent" },
        { ITEM_CLASS_MISC, "Miscellaneous" },
    };

    static const std::unordered_map<uint32, std::unordered_map<uint32, std::string>> itemSubclassNames = {
        { ITEM_CLASS_CONSUMABLE, {
            { 0, "Consumable" }, { 1, "Potion" }, { 2, "Elixir" }, { 3, "Flask" }, { 4, "Scroll" }, { 5, "Food" },
            { 6, "Item Enhancement" }, { 7, "Bandage" }, { 8, "Other" }
        }},
        { ITEM_CLASS_CONTAINER, {
            { 0, "Container" }, { 1, "Soul Container" }, { 2, "Herb Container" }, { 3, "Enchanting Container" },
            { 4, "Engineering Container" }, { 5, "Gem Container" }, { 6, "Mining Container" },
            { 7, "Leatherworking Container" }
        }},
        { ITEM_CLASS_WEAPON, {
            { 0, "Axe (1H)" }, { 1, "Axe (2H)" }, { 2, "Bow" }, { 3, "Gun" }, { 4, "Mace (1H)" }, { 5, "Mace (2H)" },
            { 6, "Polearm" }, { 7, "Sword (1H)" }, { 8, "Sword (2H)" }, { 9, "Obsolete" }, { 10, "Staff" },
            { 11, "Exotic (1H)" }, { 12, "Exotic (2H)" }, { 13, "Fist Weapon" }, { 14, "Miscellaneous" },
            { 15, "Dagger" }, { 16, "Thrown" }, { 17, "Spear" }, { 18, "Crossbow" }, { 19, "Wand" }, { 20, "Fishing Pole" }
        }},
        { ITEM_CLASS_GEM, {
            { 0, "Gem" }
        }},
        { ITEM_CLASS_ARMOR, {
            { 0, "Miscellaneous" }, { 1, "Cloth" }, { 2, "Leather" }, { 3, "Mail" }, { 4, "Plate" }, { 5, "Buckler" },
            { 6, "Shield" }, { 7, "Libram" }, { 8, "Idol" }, { 9, "Totem" }
        }},
        { ITEM_CLASS_REAGENT, {
            { 0, "Reagent" }
        }},
        { ITEM_CLASS_PROJECTILE, {
            { 0, "Wand" }, { 1, "Bolt" }, { 2, "Arrow" }, { 3, "Bullet" }, { 4, "Thrown" }
        }},
        { ITEM_CLASS_TRADE_GOODS, {
            { 0, "Trade Goods" }, { 1, "Parts" }, { 2, "Explosives" }, { 3, "Devices" },
            { 5, "Cloth" }, { 6, "Leather" }, { 7, "Metal & Stone" }, { 8, "Meat" }, { 9, "Herb" },
            { 10, "Elemental" }, { 11, "Other" }, { 12, "Enchanting" }, { 13, "Material" }
        }},
        { ITEM_CLASS_GENERIC, {
            { 0, "Generic" }
        }},
        { ITEM_CLASS_RECIPE, {
            { 0, "Book" }, { 1, "Leatherworking Pattern" }, { 2, "Tailoring Pattern" }, { 3, "Engineering Schematic" },
            { 4, "Blacksmithing" }, { 5, "Cooking Recipe" }, { 6, "Alchemy Recipe" }, { 7, "First Aid Manual" },
            { 8, "Enchanting Formula" }, { 9, "Fishing Manual" }, { 10, "Jewelcrafting Recipe" }
        }},
        { ITEM_CLASS_MONEY, {
            { 0, "Money" }
        }},
        { ITEM_CLASS_QUIVER, {
            { 0, "Quiver0" }, { 1, "Quiver1" }, { 2, "Quiver" }, { 3, "Ammo Pouch" }
        }},
        { ITEM_CLASS_QUEST, {
            { 0, "Quest" }
        }},
        { ITEM_CLASS_KEY, {
            { 0, "Key" }, { 1, "Lockpick" }
        }},
        { ITEM_CLASS_PERMANENT, {
            { 0, "Permanent" }
        }},
        { ITEM_CLASS_MISC, {
            { 0, "Junk" }, { 1, "Reagent" }, { 2, "Pet" }, { 3, "Holiday" }, { 4, "Other" }, { 5, "Mount" }
        }},
    };



    out2 << "class: ";
    auto classIt = itemClassNames.find(proto->Class);
    if (classIt != itemClassNames.end())
        out2 << classIt->second;
    else
        out2 << "Unknown(" << proto->Class << ")";

    out2 << ", subclass: ";
    auto subclassMapIt = itemSubclassNames.find(proto->Class);
    if (subclassMapIt != itemSubclassNames.end())
    {
        auto subIt = subclassMapIt->second.find(proto->SubClass);
        if (subIt != subclassMapIt->second.end())
            out2 << subIt->second;
        else
            out2 << "Unknown(" << proto->SubClass << ")";
    }
    else
    {
        out2 << "Unknown(" << proto->SubClass << ")";
    }

    ai->TellPlayerNoFacing(requester, out2);

    std::ostringstream out3;

    //Print the items contained inside item.


    ai->TellPlayerNoFacing(requester, out3);

    return true;
}

bool DebugAction::HandleLoot(Event& event, Player* requester, const std::string& text)
{
    std::string param = "";
    if (text.length() > 6)
        param = text.substr(5);

    bool doAction = ai->DoSpecificAction("add all loot", Event("debug", param), true);

    if (doAction)
        ai->TellPlayerNoFacing(requester, "Added new loot");
    else
        ai->TellPlayerNoFacing(requester, "No new loot");

    LootObjectStack* loots = AI_VALUE(LootObjectStack*, "available loot");

    for (int i = 0; i < 2000; i++)
    {
        LootObject loot = loots->GetLoot();

        if (loot.IsEmpty())
            break;

        WorldObject* wo = ai->GetWorldObject(loot.guid);

        if (wo)
            ai->TellPlayerNoFacing(requester, chat->formatWorldobject(wo) + " " + (loot.IsLootPossible(bot) ? "can loot" : "can not loot"));
        else
            ai->TellPlayerNoFacing(requester,
                                   std::to_string(loot.guid) + " " + (loot.IsLootPossible(bot) ? "can loot" : "can not loot") + " " + std::to_string(loot.guid.GetEntry()));

        if (loot.guid.IsGameObject())
        {
            GameObject* go = ai->GetGameObject(loot.guid);

            if (go->ActivateToQuest(bot))
                ai->TellPlayerNoFacing(requester, std::to_string(go->GetGoType()) + " for quest");
            else
                ai->TellPlayerNoFacing(requester, std::to_string(go->GetGoType()));
        }

        loots->Remove(loot.guid);
    }

    ai->DoSpecificAction("add all loot", Event(), true);

    return true;
}

bool DebugAction::HandleDrops(Event& event, Player* requester, const std::string& text)
{
    std::string textSubstr;
    std::ostringstream out;
    for (auto entry : chat->parseWorldEntries(textSubstr = text.substr(6)))
    {
        std::list<uint32> itemIds = GAI_VALUE2(std::list<uint32>, "entry loot list", entry);

        if (itemIds.empty())
            out << chat->formatWorldEntry(entry) << " no drops found.";
        else
            out << chat->formatWorldEntry(entry) << " " << std::to_string(itemIds.size()) << " drops found:";

        ai->TellPlayerNoFacing(requester, out);
        out.str("");
        out.clear();

        std::vector<std::pair<uint32, float>> chances;

        for (auto itemId : itemIds)
        {
            std::vector<std::string> qualifiers = { std::to_string(entry) , std::to_string(itemId) };
            std::string qualifier = Qualified::MultiQualify(qualifiers, " ");
            float chance = GAI_VALUE2(float, "loot chance", qualifier);
            if (chance > 0 && sObjectMgr.GetItemPrototype(itemId))
            {
                chances.push_back(std::make_pair(itemId, chance));
            }
        }

        std::sort(chances.begin(), chances.end(), [](std::pair<uint32, float> i, std::pair<int32, float> j) {return i.second > j.second; });

        chances.resize(std::min(20, (int)chances.size()));

        for (auto chance : chances)
        {
            out << chat->formatItem(sObjectMgr.GetItemPrototype(chance.first), 0, 0) << ": " << chance.second << "%";
            ai->TellPlayerNoFacing(requester, out);
            out.str("");
            out.clear();
        }
    }
    return true;
}

bool DebugAction::HandlePrice(Event& event, Player* requester, const std::string& text)
{
    std::ostringstream out;

    if (text.size() < 7)
        return false;

    std::string link = text.substr(6);
    ItemIds ids = ChatHelper::parseItems(link);

    for (auto id : ids)
    {
        std::ostringstream out;

        const ItemPrototype* proto = sObjectMgr.GetItemPrototype(id);
        if (!proto)
            continue;

        out << ChatHelper::formatItem(proto);
        out << " Buy from vendor: " << ChatHelper::formatMoney(proto->BuyPrice);
        out << " Sell to vendor: " << ChatHelper::formatMoney(proto->SellPrice);
        out << " Median buyout from AH (price per item): " << ChatHelper::formatMoney(ItemUsageValue::GetAHMedianBuyoutPricePerItem(proto));
        out << " Lowest AH listing buyout (price per item): " << ChatHelper::formatMoney(ItemUsageValue::GetAHListingLowestBuyoutPricePerItem(proto));
        out << " Sell to AH: " << ChatHelper::formatMoney(ItemUsageValue::GetBotAHSellMinPrice(proto)) << " to " << ChatHelper::formatMoney(ItemUsageValue::GetBotAHSellMaxPrice(proto));

        ai->TellPlayerNoFacing(requester, out);
    }

    return true;
}

bool DebugAction::HandleTrade(Event& event, Player* requester, const std::string& text)
{
    std::string param = "";
    if (text.length() > 6)
    {
        param = text.substr(6);
    }

    if (param.find("stop") == 0)
    {
        if (bot->GetTradeData())
            bot->TradeCancel(true);
    }
    return true;
}

bool DebugAction::HandleMail(Event& event, Player* requester, const std::string& text)
{
    std::string param = "";
    if (text.length() > 5)
    {
        param = text.substr(6);
    }

    bool doAction = ai->DoSpecificAction("mail", Event("debug", param.empty() ? "?" : param), true);
    return doAction;
}
