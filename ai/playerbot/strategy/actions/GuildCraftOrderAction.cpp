#include "playerbot/playerbot.h"
#include "GuildCraftOrderAction.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/CraftValues.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/strategy/values/LootValues.h"
#include "playerbot/GuildOrderSources.h"
#include "playerbot/GuildOrderCraft.h"
#include "runtime/GuildOrderStore.h"
#include "runtime/AltBotService.h"
#include "runtime/policy/provision/CraftOrderPolicy.h"

using namespace ai;
using namespace TortoiseBots::GuildOrders;

uint32 GuildCraftOrderAction::FindCraftSpell(uint32 itemId)
{
    return ai::FindCraftSpellFor(bot, itemId);
}

uint32 GuildCraftOrderAction::FindCraftEffect(uint32 spellId, uint32 itemId)
{
    return ai::FindCraftEffectFor(spellId, itemId);
}

uint32 GuildCraftOrderAction::CountFreeBagSlots()
{
    uint32 used = 0;
    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            ++used;
    }

    std::vector<Bag*> bags;
    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        Bag* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (!bag)
            continue;
        ItemPrototype const* bagProto = bag->GetProto();
        if (bagProto && bagProto->Class == ITEM_CLASS_CONTAINER && bagProto->SubClass == ITEM_SUBCLASS_CONTAINER)
            bags.push_back(bag);
    }

    return TortoiseBots::CraftOrderPolicy::FreeBagSlots(
        used, INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START, bags,
        [](Bag* bag) { return bag->GetFreeSlots(); });
}

GameObject* GuildCraftOrderAction::FindFocusObject(uint32 focusId, float range)
{
    GameObject* best = nullptr;
    float bestDist = range;

    std::list<ObjectGuid> objects = AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los");
    for (ObjectGuid guid : objects)
    {
        GameObject* go = ai->GetGameObject(guid);
        if (!go || !sServerFacade.isSpawned(go))
            continue;

        GameObjectInfo const* info = sObjectMgr.GetGameObjectInfo(go->GetEntry());
        if (!info || info->type != GAMEOBJECT_TYPE_SPELL_FOCUS || info->spellFocus.focusId != focusId)
            continue;

        float dist = bot->GetDistance(go);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = go;
        }
    }

    return best;
}

bool GuildCraftOrderAction::Execute(Event& event)
{
    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (!order.IsCraftOrder())
        return false;

    uint32 botGuidLow = bot->GetGUIDLow();
    std::optional<OrderRecord> record = TortoiseBots::GuildOrders::Find(botGuidLow);
    if (record && record->state != OrderState::Working)
        return false;

    uint32 itemId = order.itemId;
    uint32 orderAmount = order.amount;
    uint32 delivered = record ? std::min(record->delivered, record->amount) : 0;
    if (!itemId || !orderAmount)
        return false;

    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId) + delivered;
    if (currentCount >= orderAmount)
    {
        if (record)
            TortoiseBots::GuildOrders::SetState(botGuidLow, OrderState::Delivering, OrderReason::None);
        return true;
    }

    if (bot->IsNonMeleeSpellCasted(false, true, true))
        return false;

    uint32 craftSpellId = FindCraftSpell(itemId);
    if (!craftSpellId)
    {
        ai->TellDebug(ai->GetMaster(), "Guild craft order: I don't know how to craft: " + order.target, "debug travel");
        if (record)
            TortoiseBots::GuildOrders::SetState(botGuidLow, OrderState::Cant, OrderReason::NoRecipe);
        return false;
    }

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(craftSpellId);
    if (!pSpellInfo)
        return false;

    uint32 remaining = orderAmount - currentCount;
    uint32 casts = ai::CraftCastsForRemaining(craftSpellId, itemId, remaining);

    for (uint8 i = 0; i < MAX_SPELL_TOTEMS; ++i)
    {
        uint32 totem = pSpellInfo->Totem[i];
        if (totem && !bot->HasItemCount(totem, 1))
        {
            if (ai->HasCheat(BotCheatMask::item))
                ai->EnsureCheatItem(totem, 1);
            if (!bot->HasItemCount(totem, 1))
                return false;
        }
    }

    // Affordable casts first: what the bags hold right now decides whether
    // the bot crafts, gathers more, or fails. Shortfalls below only matter
    // when more reagents can still be taken.
    uint32 affordable = casts;
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!pSpellInfo->ReagentCount[i] || pSpellInfo->Reagent[i] <= 0)
            continue;

        uint32 have = ai->GetInventoryItemsCountWithId(pSpellInfo->Reagent[i]);
        affordable = std::min(affordable,
            TortoiseBots::CraftOrderPolicy::CastsAffordable(have, pSpellInfo->ReagentCount[i]));
    }

    // Fail early, before spending anything: every farm reagent's verdict
    // first, so a dead reagent stops the order before vendor gold is spent.
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!pSpellInfo->ReagentCount[i] || pSpellInfo->Reagent[i] <= 0)
            continue;

        uint32 reagentId = pSpellInfo->Reagent[i];
        ItemPrototype const* reagentProto = sObjectMgr.GetItemPrototype(reagentId);
        if (reagentProto && ItemUsageValue::IsItemSoldByAnyVendor(reagentProto) &&
            !ItemUsageValue::IsItemSoldByAnyVendorButHasLimitedMaxCount(reagentProto))
            continue;

        uint32 have = ai->GetInventoryItemsCountWithId(reagentId);
        if (!TortoiseBots::CraftOrderPolicy::ReagentShortfall(pSpellInfo->ReagentCount[i], casts, have))
            continue;

        SourcePlan plan = ai::PlanGuildOrderSources(bot, reagentId, OrderType::Farm);
        if (plan.verdict != OrderReason::None)
        {
            ai->TellDebug(ai->GetMaster(), "Guild craft order: cannot source reagent " + std::to_string(reagentId), "debug travel");
            if (record)
                TortoiseBots::GuildOrders::SetState(botGuidLow, StateForReason(plan.verdict), plan.verdict);
            return false;
        }
    }

    // Vendor reagents: only what the bot can fully afford for at least one
    // cast, capped to free bag space. Anything else fails now, not after
    // a trip and partial gold. Real gold only: orders never cheat.
    bool autonomous = TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid());
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!pSpellInfo->ReagentCount[i] || pSpellInfo->Reagent[i] <= 0)
            continue;

        uint32 reagentId = pSpellInfo->Reagent[i];
        ItemPrototype const* reagentProto = sObjectMgr.GetItemPrototype(reagentId);
        if (!reagentProto || !ItemUsageValue::IsItemSoldByAnyVendor(reagentProto) ||
            ItemUsageValue::IsItemSoldByAnyVendorButHasLimitedMaxCount(reagentProto))
            continue;

        uint32 have = ai->GetInventoryItemsCountWithId(reagentId);
        uint32 shortfall = TortoiseBots::CraftOrderPolicy::ReagentShortfall(pSpellInfo->ReagentCount[i], casts, have);
        if (!shortfall)
            continue;

        bool fits = AI_VALUE2(uint32, "stack space for item", reagentId) > 0;
        if (affordable >= 1 && !fits)
            continue;

        // One full-bags rule: a missing reagent that cannot fit with no
        // craftable batch delivers what is held, or fails stuck.
        TortoiseBots::CraftOrderPolicy::FullBagsDecision fullBags =
            TortoiseBots::CraftOrderPolicy::DecideFullBags(fits, affordable,
                ai->GetInventoryItemsCountWithId(itemId));
        if (fullBags != TortoiseBots::CraftOrderPolicy::FullBagsDecision::KeepGoing)
        {
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

        if (autonomous)
        {
            ai->TellDebug(ai->GetMaster(), "Guild craft order: autonomous bot cannot buy reagent " + std::to_string(reagentId), "debug travel");
            if (record)
                TortoiseBots::GuildOrders::SetState(botGuidLow, StateForReason(OrderReason::NoSource), OrderReason::NoSource);
            return false;
        }

        uint32 oneCast = TortoiseBots::CraftOrderPolicy::ReagentShortfall(pSpellInfo->ReagentCount[i], 1, have);
        uint32 buyCount = reagentProto->BuyCount ? reagentProto->BuyCount : 1;
        uint64_t bundles = TortoiseBots::CraftOrderPolicy::BundlesForShortfall(oneCast, buyCount);
        if (bundles * reagentProto->BuyPrice > bot->GetMoney())
        {
            ai->TellDebug(ai->GetMaster(), "Guild craft order: cannot afford reagent " + std::to_string(reagentId), "debug travel");
            if (record)
                TortoiseBots::GuildOrders::SetState(botGuidLow, StateForReason(OrderReason::NoSource), OrderReason::NoSource);
            return false;
        }

        ai->TellDebug(ai->GetMaster(), "Guild craft order: buying reagent " + std::to_string(reagentId) + " from vendor", "debug travel");
        return false;
    }

    uint32 farmItem = AI_VALUE(uint32, "guild order farm item");
    if (farmItem)
    {
        bool fits = AI_VALUE2(uint32, "stack space for item", farmItem) > 0;
        TortoiseBots::CraftOrderPolicy::FullBagsDecision fullBags =
            TortoiseBots::CraftOrderPolicy::DecideFullBags(fits, affordable,
                ai->GetInventoryItemsCountWithId(itemId));
        if (fullBags != TortoiseBots::CraftOrderPolicy::FullBagsDecision::KeepGoing)
        {
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

        ai->TellDebug(ai->GetMaster(), "Guild craft order: farming reagent " + std::to_string(farmItem), "debug travel");
        return false;
    }

    if (pSpellInfo->RequiresSpellFocus)
    {
        GameObject* focus = FindFocusObject(pSpellInfo->RequiresSpellFocus, sPlayerbotAIConfig.sightDistance);
        if (!focus)
        {
            ai->TellDebug(ai->GetMaster(), "Guild craft order: no workshop in sight", "debug travel");
            return false;
        }

        if (!bot->IsWithinDistInMap(focus, 8.0f))
        {
            float x, y, z;
            focus->GetPosition(x, y, z);
            MoveTo(focus->GetMapId(), x, y, z, false, false);
            return true;
        }
    }

    // Craft from what is in the bags: the limiting reagent caps the batch,
    // so a full bag of partial reagents still crafts instead of stalling.
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    uint32 stackSize = proto ? proto->GetMaxStackSize() : 1;
    if (!stackSize)
        stackSize = 1;
    uint32 effect = FindCraftEffect(craftSpellId, itemId);
    uint32 maxYield = TortoiseBots::CraftOrderPolicy::MaxYieldPerCast(
        pSpellInfo->EffectBasePoints[effect], pSpellInfo->EffectBaseDice[effect],
        pSpellInfo->EffectDieSides[effect]);
    uint32 batch = TortoiseBots::CraftOrderPolicy::BatchForFreeSlots(affordable, maxYield, stackSize, CountFreeBagSlots());
    if (!batch)
    {
        if (!record)
            return false;
        TortoiseBots::CraftOrderPolicy::FullBagsDecision fullBags =
            TortoiseBots::CraftOrderPolicy::DecideFullBags(false, 0,
                ai->GetInventoryItemsCountWithId(itemId));
        if (fullBags == TortoiseBots::CraftOrderPolicy::FullBagsDecision::Deliver)
            TortoiseBots::GuildOrders::SetState(botGuidLow, OrderState::Delivering, OrderReason::BagsFull);
        else
            TortoiseBots::GuildOrders::SetState(botGuidLow,
                StateForReason(OrderReason::Stuck), OrderReason::Stuck);
        return false;
    }

    std::ostringstream cmd;
    cmd << "castnc " << craftSpellId << " " << batch;
    ai->HandleCommand(CHAT_MSG_WHISPER, cmd.str(), *bot);

    ai->TellDebug(ai->GetMaster(), "Guild craft order: crafting " + std::to_string(batch) + "x " + std::string(proto ? proto->Name1 : order.target), "debug travel");

    return true;
}

bool GuildCraftOrderAction::isUseful()
{
    if (!bot->GetGuildId())
        return false;

    if (bot->IsMoving())
        return false;

    if (bot->IsMounted())
        return false;

    if (bot->IsNonMeleeSpellCasted(false, true, true))
        return false;

    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (!order.IsCraftOrder())
        return false;

    uint32 botGuidLow = bot->GetGUIDLow();
    std::optional<OrderRecord> record = TortoiseBots::GuildOrders::Find(botGuidLow);
    if (record && record->state != OrderState::Working)
        return false;

    uint32 itemId = order.itemId;
    uint32 orderAmount = order.amount;
    if (!itemId || !orderAmount)
        return false;

    return true;
}
