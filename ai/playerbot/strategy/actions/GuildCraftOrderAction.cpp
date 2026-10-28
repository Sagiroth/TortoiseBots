#include "playerbot/playerbot.h"
#include "GuildCraftOrderAction.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/CraftValues.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/strategy/values/LootValues.h"
#include "playerbot/GuildOrderSources.h"
#include "../../../../runtime/GuildOrderStore.h"
#include "../../../../runtime/policy/provision/CraftOrderPolicy.h"

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

bool GuildCraftOrderAction::Execute(Event& event)
{
    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (!order.IsCraftOrder())
        return false;

    uint32 botGuidLow = bot->GetGUIDLow();
    std::optional<OrderRecord> record = TortoiseBots::GuildOrders::Find(botGuidLow);
    uint32 itemId = record ? record->itemId : GuildOrderValue::FindItemByName(order.target);
    uint32 orderAmount = record ? record->amount : order.amount;
    uint32 delivered = record ? std::min(record->delivered, record->amount) : 0;
    if (!itemId || !orderAmount)
        return false;

    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId) + delivered;
    if (currentCount >= orderAmount)
    {
        if (record)
            TortoiseBots::GuildOrders::SetState(botGuidLow, OrderState::Delivering, OrderReason::None);
        return false;
    }

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

    bool missingVendorOnly = false;
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
            missingVendorOnly = true;
            continue;
        }

        SourcePlan plan = ai::PlanGuildOrderSources(bot, reagentId, OrderType::Farm);
        if (plan.verdict != OrderReason::None)
        {
            ai->TellDebug(ai->GetMaster(), "Guild craft order: cannot source reagent " + std::to_string(reagentId), "debug travel");
            if (record)
                TortoiseBots::GuildOrders::SetState(botGuidLow, OrderState::Cant, plan.verdict);
            return false;
        }

        SET_AI_VALUE2(int32, "manual int", "guild order farm item", (int32)reagentId);
        ai->TellDebug(ai->GetMaster(), "Guild craft order: farming reagent " + std::to_string(reagentId), "debug travel");
        return false;
    }

    RESET_AI_VALUE2(int32, "manual int", "guild order farm item");

    if (missingVendorOnly)
    {
        ai->TellDebug(ai->GetMaster(), "Guild craft order: waiting on vendor reagents", "debug travel");
        return false;
    }

    if (pSpellInfo->RequiresSpellFocus && !WorldPosition(bot).HasAreaFlag(AREA_FLAG_CAPITAL))
    {
        ai->TellDebug(ai->GetMaster(), "Guild craft order: needs a city workshop", "debug travel");
        return false;
    }

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    uint32 stackSize = proto ? proto->GetMaxStackSize() : 1;
    if (!stackSize)
        stackSize = 1;
    uint32 maxYield = TortoiseBots::CraftOrderPolicy::MaxYieldPerCast(
        pSpellInfo->EffectBasePoints[effect], pSpellInfo->EffectBaseDice[effect],
        pSpellInfo->EffectDieSides[effect]);
    uint32 freeSlots = AI_VALUE(uint8, "bag space") < 100 ? 1 : 0;
    if (!TortoiseBots::CraftOrderPolicy::FitsInFreeSlots(casts, maxYield, stackSize, freeSlots) &&
        AI_VALUE2(uint32, "stack space for item", itemId) < remaining)
        return false;

    std::ostringstream cmd;
    cmd << "castnc " << craftSpellId << " " << casts;
    ai->HandleCommand(CHAT_MSG_WHISPER, cmd.str(), *bot);

    ai->TellDebug(ai->GetMaster(), "Guild craft order: crafting " + std::to_string(casts) + "x " + std::string(proto ? proto->Name1 : order.target), "debug travel");

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

    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (!order.IsCraftOrder())
        return false;

    uint32 botGuidLow = bot->GetGUIDLow();
    std::optional<OrderRecord> record = TortoiseBots::GuildOrders::Find(botGuidLow);
    uint32 itemId = record ? record->itemId : GuildOrderValue::FindItemByName(order.target);
    uint32 orderAmount = record ? record->amount : order.amount;
    uint32 delivered = record ? std::min(record->delivered, record->amount) : 0;
    if (!itemId || !orderAmount)
        return false;

    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId) + delivered;
    if (currentCount >= orderAmount)
        return false;

    return true;
}
