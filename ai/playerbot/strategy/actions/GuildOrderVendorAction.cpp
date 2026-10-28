#include "playerbot/playerbot.h"
#include "GuildOrderVendorAction.h"
#include "SellAction.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/GuildOrderCraft.h"
#include "runtime/GuildOrderStore.h"
#include "runtime/policy/provision/CraftOrderPolicy.h"

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

    uint32 craftSpellId = ai::FindCraftSpellFor(bot, itemId);
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

    uint32 casts = ai::CraftCastsForRemaining(craftSpellId, itemId, orderAmount - currentCount);
    uint32 have = ai->GetInventoryItemsCountWithId(reagentId);
    uint32 shortfall = TortoiseBots::CraftOrderPolicy::ReagentShortfall(perCast, casts, have);
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

        // Total cost up front: never spend gold on a partial buy that fails
        // the order on the next tick. One cast's worth is the minimum.
        // Real gold only: orders never cheat.
        uint32 buyCount = reagentProto->BuyCount ? reagentProto->BuyCount : 1;
        uint32 price = uint32(floor(reagentProto->BuyPrice * bot->GetReputationPriceDiscount(pCreature)));
        uint32 oneCast = TortoiseBots::CraftOrderPolicy::ReagentShortfall(perCast, 1, have);
        uint32 minBundles = TortoiseBots::CraftOrderPolicy::BundlesForShortfall(oneCast, buyCount);
        uint32 money = bot->GetMoney();
        if (uint64_t(minBundles) * price > money)
        {
            if (record)
                TortoiseBots::GuildOrders::SetState(botGuidLow,
                    StateForReason(OrderReason::NoSource), OrderReason::NoSource);
            return false;
        }

        // Cap the buy to what fits: a failed store is a wasted trip tick.
        uint32 room = AI_VALUE2(uint32, "stack space for item", reagentId);
        if (room == 0)
        {
            // One full-bags rule: nothing more fits, so deliver what is
            // held or fail stuck - never idle at the vendor.
            TortoiseBots::CraftOrderPolicy::FullBagsDecision fullBags =
                TortoiseBots::CraftOrderPolicy::DecideFullBags(false, 0,
                    ai->GetInventoryItemsCountWithId(itemId));
            if (record)
            {
                if (fullBags == TortoiseBots::CraftOrderPolicy::FullBagsDecision::Deliver)
                    TortoiseBots::GuildOrders::SetState(botGuidLow, OrderState::Delivering, OrderReason::BagsFull);
                else
                    TortoiseBots::GuildOrders::SetState(botGuidLow,
                        StateForReason(OrderReason::Stuck), OrderReason::Stuck);
            }
            return false;
        }

        uint32 capped = std::min(shortfall, room);
        uint32 bundles = TortoiseBots::CraftOrderPolicy::BundlesForShortfall(capped, buyCount);
        while (bundles > minBundles && uint64_t(bundles) * price > money)
            --bundles;

        uint32 bought = 0;
        for (uint32 b = 0; b < bundles; ++b)
        {
            uint32 before = ai->GetInventoryItemsCountWithId(reagentId);
            bot->BuyItemFromVendor(vendorguid, reagentId, 1, NULL_BAG, NULL_SLOT);
            uint32 after = ai->GetInventoryItemsCountWithId(reagentId);
            if (after <= before)
                break;
            bought += after - before;
        }

        // A buy that moves nothing with room and bundles fails the order:
        // retrying the same vendor tick after tick never pays.
        if (bought == 0 && bundles > 0 && room > 0)
        {
            if (record)
                TortoiseBots::GuildOrders::SetState(botGuidLow,
                    StateForReason(OrderReason::NoSource), OrderReason::NoSource);
            return false;
        }

        RESET_AI_VALUE(uint32, "guild order vendor reagent");
        ai->TellDebug(ai->GetMaster(), "Guild order: bought " + std::to_string(bought) + "x " + reagentProto->Name1 + " from vendor", "debug travel");
        return bought > 0;
    }

    if (record)
        TortoiseBots::GuildOrders::SetState(botGuidLow,
            StateForReason(OrderReason::NoSource), OrderReason::NoSource);
    return false;
}
