#include "playerbot/playerbot.h"
#include "GuildOrderVendorAction.h"
#include "SellAction.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/TravelMgr.h"
#include "../../../../runtime/GuildOrderStore.h"
#include "../../../../runtime/policy/provision/CraftOrderPolicy.h"

using namespace ai;
using namespace TortoiseBots::GuildOrders;

bool GuildOrderVendorAction::isUseful()
{
    if (!bot->GetGuildId() || bot->IsMoving())
        return false;

    if (AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL)
        return false;

    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (!order.IsCraftOrder())
        return false;

    uint32 reagentId = AI_VALUE(uint32, "guild order vendor reagent");
    if (!reagentId)
        return false;

    std::list<ObjectGuid> vendors = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
    for (ObjectGuid vendorguid : vendors)
    {
        Creature* pCreature = bot->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_VENDOR);
        if (!pCreature || !SellAction::HasVendorStock(pCreature))
            continue;

        VendorItemData const* vItems = pCreature->GetVendorItems();
        if (vItems && vItems->FindItemSlot(reagentId) < vItems->GetItemCount())
            return true;
        VendorItemData const* tItems = pCreature->GetVendorTemplateItems();
        if (tItems && tItems->FindItemSlot(reagentId) < tItems->GetItemCount())
            return true;
    }

    return false;
}

bool GuildOrderVendorAction::Execute(Event& event)
{
    uint32 botGuidLow = bot->GetGUIDLow();
    std::optional<OrderRecord> record = TortoiseBots::GuildOrders::Find(botGuidLow);
    if (record && record->state != OrderState::Working)
        return false;

    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (!order.IsCraftOrder())
        return false;

    uint32 reagentId = AI_VALUE(uint32, "guild order vendor reagent");
    if (!reagentId)
        return false;

    ItemPrototype const* reagentProto = sObjectMgr.GetItemPrototype(reagentId);
    if (!reagentProto)
        return false;

    uint32 itemId = order.itemId;
    uint32 orderAmount = order.amount;
    if (!itemId || !orderAmount)
        return false;

    uint32 delivered = record ? std::min(record->delivered, record->amount) : 0;
    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId) + delivered;
    if (currentCount >= orderAmount)
        return false;

    uint32 craftSpellId = 0;
    for (auto& [spellId, spellState] : bot->GetSpellMap())
    {
        if (spellState.state == PLAYERSPELL_REMOVED || spellState.disabled || IsPassiveSpell(spellId))
            continue;

        const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
        if (!pSpellInfo)
            continue;

        for (int i = 0; i < 3; ++i)
        {
            if (pSpellInfo->Effect[i] == SPELL_EFFECT_CREATE_ITEM && pSpellInfo->EffectItemType[i] == itemId)
            {
                craftSpellId = spellId;
                break;
            }
        }
        if (craftSpellId)
            break;
    }
    if (!craftSpellId)
        return false;

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(craftSpellId);
    if (!pSpellInfo)
        return false;

    uint32 perCast = 0;
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (pSpellInfo->Reagent[i] == (int32)reagentId)
            perCast = std::max(perCast, pSpellInfo->ReagentCount[i]);
    }
    if (!perCast)
        return false;

    uint32 have = ai->GetInventoryItemsCountWithId(reagentId);
    uint32 shortfall = TortoiseBots::CraftOrderPolicy::ReagentShortfall(perCast, orderAmount - currentCount, have);
    if (!shortfall)
        return false;

    std::list<ObjectGuid> vendors = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
    for (ObjectGuid vendorguid : vendors)
    {
        Creature* pCreature = bot->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_VENDOR);
        if (!pCreature || !SellAction::HasVendorStock(pCreature))
            continue;

        VendorItemData const* vItems = pCreature->GetVendorItems();
        VendorItemData const* tItems = pCreature->GetVendorTemplateItems();
        bool sellsHere = (vItems && vItems->FindItemSlot(reagentId) < vItems->GetItemCount()) ||
            (tItems && tItems->FindItemSlot(reagentId) < tItems->GetItemCount());
        if (!sellsHere)
            continue;

        sServerFacade.SetFacingTo(bot, pCreature);

        uint32 buyCount = reagentProto->BuyCount ? reagentProto->BuyCount : 1;
        uint32 bundles = (shortfall + buyCount - 1) / buyCount;
        uint32 bought = 0;
        while (bundles--)
        {
            uint32 price = uint32(floor(reagentProto->BuyPrice * bot->GetReputationPriceDiscount(pCreature)));
            if (bot->GetMoney() < price)
            {
                if (record && bought == 0)
                    TortoiseBots::GuildOrders::SetState(botGuidLow, OrderState::Cant, OrderReason::NoSource);
                return bought > 0;
            }

            uint32 before = ai->GetInventoryItemsCountWithId(reagentId);
            bot->BuyItemFromVendor(vendorguid, reagentId, 1, NULL_BAG, NULL_SLOT);
            uint32 after = ai->GetInventoryItemsCountWithId(reagentId);
            if (after <= before)
                return bought > 0;
            bought += after - before;
        }

        RESET_AI_VALUE(uint32, "guild order vendor reagent");
        ai->TellDebug(ai->GetMaster(), "Guild order: bought " + std::to_string(bought) + "x " + reagentProto->Name1 + " from vendor", "debug travel");
        return true;
    }

    if (record)
        TortoiseBots::GuildOrders::SetState(botGuidLow, OrderState::Cant, OrderReason::NoSource);
    return false;
}
