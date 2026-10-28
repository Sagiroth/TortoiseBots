#include "playerbot/playerbot.h"
#include "GuildCraftOrderAction.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/CraftValues.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/strategy/values/LootValues.h"
#include "playerbot/GuildOrderSources.h"
#include "runtime/GuildOrderStore.h"
#include "runtime/policy/provision/CraftOrderPolicy.h"

using namespace ai;
using namespace TortoiseBots::GuildOrders;

uint32 GuildCraftOrderAction::FindCraftSpell(uint32 itemId)
{
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
                return spellId;
        }
    }

    return 0;
}

uint32 GuildCraftOrderAction::FindCraftEffect(uint32 spellId, uint32 itemId)
{
    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
    if (!pSpellInfo)
        return 0;

    for (int i = 0; i < 3; ++i)
    {
        if (pSpellInfo->Effect[i] == SPELL_EFFECT_CREATE_ITEM && pSpellInfo->EffectItemType[i] == itemId)
            return i;
    }

    return 0;
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

    std::list<ObjectGuid> objects = AI_VALUE(std::list<ObjectGuid>, "nearest game objects");
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

    uint32 effect = FindCraftEffect(craftSpellId, itemId);
    uint32 yieldPerCast = TortoiseBots::CraftOrderPolicy::MinYieldPerCast(
        pSpellInfo->EffectBasePoints[effect], pSpellInfo->EffectBaseDice[effect],
        pSpellInfo->EffectDieSides[effect]);
    uint32 remaining = orderAmount - currentCount;
    uint32 casts = TortoiseBots::CraftOrderPolicy::CastsForRemaining(remaining, yieldPerCast);

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

    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!pSpellInfo->ReagentCount[i] || pSpellInfo->Reagent[i] <= 0)
            continue;

        uint32 reagentId = pSpellInfo->Reagent[i];
        uint32 have = ai->GetInventoryItemsCountWithId(reagentId);
        uint32 shortfall = TortoiseBots::CraftOrderPolicy::ReagentShortfall(pSpellInfo->ReagentCount[i], casts, have);
        if (!shortfall)
            continue;

        ItemPrototype const* reagentProto = sObjectMgr.GetItemPrototype(reagentId);
        if (reagentProto && ItemUsageValue::IsItemSoldByAnyVendor(reagentProto) &&
            !ItemUsageValue::IsItemSoldByAnyVendorButHasLimitedMaxCount(reagentProto))
        {
            ai->TellDebug(ai->GetMaster(), "Guild craft order: buying reagent " + std::to_string(reagentId) + " from vendor", "debug travel");
            return false;
        }

        SourcePlan plan = ai::PlanGuildOrderSources(bot, reagentId, OrderType::Farm);
        if (plan.verdict != OrderReason::None)
        {
            ai->TellDebug(ai->GetMaster(), "Guild craft order: cannot source reagent " + std::to_string(reagentId), "debug travel");
            if (record)
                TortoiseBots::GuildOrders::SetState(botGuidLow, StateForReason(plan.verdict), plan.verdict);
            return false;
        }

        ai->TellDebug(ai->GetMaster(), "Guild craft order: farming reagent " + std::to_string(reagentId), "debug travel");
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

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    uint32 stackSize = proto ? proto->GetMaxStackSize() : 1;
    if (!stackSize)
        stackSize = 1;
    uint32 maxYield = TortoiseBots::CraftOrderPolicy::MaxYieldPerCast(
        pSpellInfo->EffectBasePoints[effect], pSpellInfo->EffectBaseDice[effect],
        pSpellInfo->EffectDieSides[effect]);
    uint32 batch = TortoiseBots::CraftOrderPolicy::BatchForFreeSlots(casts, maxYield, stackSize, CountFreeBagSlots());
    if (!batch)
    {
        if (record)
            TortoiseBots::GuildOrders::SetState(botGuidLow, OrderState::Delivering, OrderReason::BagsFull);
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
