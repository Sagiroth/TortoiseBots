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
    (void)event;
    uint32 botGuidLow = bot->GetGUIDLow();
    std::optional<OrderRecord> record = TortoiseBots::GuildOrders::Find(botGuidLow);
    if (!record || record->state != OrderState::Working || record->supplyStep != SupplyStep::Farm)
        return false;

    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (!order.IsCraftOrder())
        return false;

    uint32 reagentId = AI_VALUE(uint32, "guild order vendor reagent");
    if (!reagentId)
        return false;
    record = TortoiseBots::GuildOrders::Find(botGuidLow);
    if (!record || record->state != OrderState::Working || record->supplyStep != SupplyStep::Farm ||
        record->supplyItemId != reagentId || !record->supplyCount)
        return false;

    ItemPrototype const* reagentProto = sObjectMgr.GetItemPrototype(reagentId);
    if (!reagentProto)
        return false;
    if (record->supplyItemId != reagentId || !record->supplyCount)
        return false;

    uint32 shortfall = record->supplyCount;
    uint32 itemId = order.itemId;
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

        // Buy the current missing batch in one stop; orders use real gold only.
        uint32 buyCount = reagentProto->BuyCount ? reagentProto->BuyCount : 1;
        uint32 price = uint32(floor(reagentProto->BuyPrice * bot->GetReputationPriceDiscount(pCreature)));
        uint32 money = bot->GetMoney();
        uint32 bundles = TortoiseBots::CraftOrderPolicy::BundlesForShortfall(shortfall, buyCount);
        if (uint64_t(bundles) * price > money)
        {
            TortoiseBots::GuildOrders::SetSupply(botGuidLow, SupplyStep::Farm, reagentId,
                shortfall, SupplyStep::None, OrderReason::NoSource);
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
        bundles = TortoiseBots::CraftOrderPolicy::BundlesForShortfall(capped, buyCount);

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
            TortoiseBots::GuildOrders::SetSupply(botGuidLow, SupplyStep::Farm, reagentId,
                shortfall, SupplyStep::None, OrderReason::NoSource);
            return false;
        }

        RESET_AI_VALUE(uint32, "guild order vendor reagent");
        if (bought > 0)
        {
            TortoiseBots::GuildOrders::NoteSupplyGain(botGuidLow, reagentId, bought);
            TortoiseBots::GuildOrders::NoteSupplyProgress(botGuidLow);
        }
        TortoiseBots::GuildOrders::SetSupply(botGuidLow, SupplyStep::Farm, reagentId,
            shortfall > bought ? shortfall - bought : 0, SupplyStep::None);
    }

    TortoiseBots::GuildOrders::SetSupply(botGuidLow, SupplyStep::Farm, reagentId,
        shortfall, SupplyStep::None, OrderReason::NoSource);
    return false;
}
