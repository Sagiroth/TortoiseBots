#include "playerbot/playerbot.h"
#include "VendorValues.h"
#include "ItemUsageValue.h"
#include "BudgetValues.h"
#include "VendorWeaponUpgradePolicy.h"
#include "runtime/HireLifecycle.h"
#include "playerbot/PlayerbotAI.h"
#include "SharedValueContext.h"

using namespace ai;

VendorMap* VendorMapValue::Calculate()
{
    VendorMap* vendorpMap = new VendorMap;


    for (uint32 entry = 0; entry < sCreatureStorage.GetMaxEntry(); entry++)
    {
        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(entry);

        if (!cInfo)
            continue;

        VendorItemList vendorItems;
        VendorItemData const* vItems = sObjectMgr.GetNpcVendorItemList(entry);

        if (vItems)
            for (auto vItem : vItems->m_items)
                if (!vItem->maxcount) //Ignore limited amount items.
                    vendorpMap->insert(std::make_pair(vItem->item, entry));

        uint32 vendorId = cInfo->vendor_id;
        if (vendorId)
        {
            vItems = sObjectMgr.GetNpcVendorTemplateItemList(vendorId);
            if (vItems)
                for (auto vItem : vItems->m_items)
                    if (!vItem->maxcount) //Ignore limited amount items.
                        vendorpMap->insert(std::make_pair(vItem->item, entry));
        }
    }

    return vendorpMap;
}

//What items does this entry have in its loot list?
std::list<int32> ItemVendorListValue::Calculate()
{
    uint32 itemId = stoi(getQualifier());

    VendorMap* vendorMap = GAI_VALUE(VendorMap*, "vendor map");

    std::list<int32> entries;

    auto range = vendorMap->equal_range(itemId);

    for (auto itr = range.first; itr != range.second; ++itr)
        entries.push_back(itr->second);

    return entries;
}

bool VendorHasUsefulItemValue::Calculate()
{
    uint32 entry = std::stoi(this->GetQualifier());
    CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(entry);

    if (!cInfo)
        return false;

    VendorItemList vendorItems;
    VendorItemData const* vItems = sObjectMgr.GetNpcVendorItemList(entry);

    if (vItems)
        for (auto vItem : vItems->m_items)
                vendorItems.push_back(vItem);

    uint32 vendorId = cInfo->vendor_id;

    if (vendorId)
    {
        vItems = sObjectMgr.GetNpcVendorTemplateItemList(vendorId);

        if (vItems)
            for (auto vItem : vItems->m_items)
                vendorItems.push_back(vItem);
    }

    std::unordered_map <ItemUsage, uint32> freeMoney;

    freeMoney[ItemUsage::ITEM_USAGE_EQUIP] = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::gear);
    freeMoney[ItemUsage::ITEM_USAGE_USE] = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::consumables);
    freeMoney[ItemUsage::ITEM_USAGE_SKILL] = freeMoney[ItemUsage::ITEM_USAGE_DISENCHANT] = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::tradeskill);
    freeMoney[ItemUsage::ITEM_USAGE_AMMO] = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::ammo);
    freeMoney[ItemUsage::ITEM_USAGE_QUEST] = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::anything);

    for (auto vendorItem : vendorItems)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(vendorItem->item);

        ItemUsage usage = AI_VALUE2_LAZY(ItemUsage, "item usage", vendorItem->item);

        if (freeMoney.find(usage) == freeMoney.end() || proto->BuyPrice > freeMoney[usage])
        {
            // Vendor stock the bot does not own yet never scores EQUIP (the
            // classifier only evaluates items it has seen), so a real weapon or
            // shield upgrade would keep this trigger dark and the fallback buy
            // loop in BuyAction would never run. Same audit rules: spec-allowed,
            // usable now, better by scoring, affordable under the trainer
            // reserve. Masterless pool bots only; owned alts (not random),
            // bots with a live master, and hired companions (whose character
            // is deleted at hire end, even mid grace period) never spend.
            if (ai->HasActivePlayerMaster() || ai->HasRealPlayerMaster() ||
                !sRandomBotFacade.IsRandomBot(bot) ||
                TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()))
                continue;
            if (!IsVendorWeaponUpgradeCandidate(proto->Class, proto->SubClass))
                continue;
            if (ai->HasItemInInventory(proto->ItemId))
                continue;
            ItemQualifier weaponQualifier(vendorItem->item);
            if (ItemUsageValue::QueryItemUsageForEquip(weaponQualifier, bot) != ItemUsage::ITEM_USAGE_EQUIP)
                continue;
            RESET_AI_VALUE2(uint32, "total money needed for", (uint32)NeedMoneyFor::spells);
            uint32 spellReserve = AI_VALUE2(uint32, "total money needed for", (uint32)NeedMoneyFor::spells);
            if (!VendorWeaponUpgradeAffordable(proto->BuyPrice, bot->GetMoney(), spellReserve))
                continue;
        }

        return true;
    }

    return false;
}
