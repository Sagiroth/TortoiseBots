
#include "playerbot/playerbot.h"
#include "GuildValues.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/GuildOrderSources.h"
#include "runtime/GuildOrderStore.h"
#include "runtime/policy/provision/CraftOrderPolicy.h"
#include "Guild/GuildMgr.h"

using namespace ai;

GuildOrder GuildOrderValue::Calculate()
{
    GuildOrder order;

    if (!bot->GetGuildId())
        return order;

    // The store is the only order source; bots never read officer notes.
    std::optional<TortoiseBots::GuildOrders::OrderRecord> stored =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!stored || stored->guildId != bot->GetGuildId() ||
        TortoiseBots::GuildOrders::IsFinished(stored->state))
        return order;

    switch (stored->type)
    {
        case TortoiseBots::GuildOrders::OrderType::Farm:
            order.type = GuildOrderType::Farm;
            break;
        case TortoiseBots::GuildOrders::OrderType::Buy:
            order.type = GuildOrderType::AuctionHouse;
            break;
        case TortoiseBots::GuildOrders::OrderType::Craft:
            order.type = GuildOrderType::Craft;
            break;
    }
    order.itemId = stored->itemId;
    order.amount = stored->amount;
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(stored->itemId);
    order.target = proto ? proto->Name1 : std::to_string(stored->itemId);
    return order;
}

bool ai::OrderStillNeedsWork(Player* bot, GuildOrder const& order)
{
    if (!bot || !order.itemId || !order.amount)
        return false;
    std::optional<TortoiseBots::GuildOrders::OrderRecord> stored =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!stored || stored->itemId != order.itemId)
        return false;
    if (stored->state == TortoiseBots::GuildOrders::OrderState::Waiting ||
        stored->state == TortoiseBots::GuildOrders::OrderState::Delivering)
        return false;
    return stored->delivered + bot->GetItemCount(order.itemId) < order.amount;
}

namespace
{
uint32 FindCraftSpellFor(Player* bot, uint32 itemId)
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
} // namespace

uint32 GuildOrderVendorReagentValue::Calculate()
{
    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (!order.IsCraftOrder())
        return 0;

    std::optional<TortoiseBots::GuildOrders::OrderRecord> record =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (record && record->state != TortoiseBots::GuildOrders::OrderState::Working)
        return 0;

    uint32 itemId = order.itemId;
    uint32 orderAmount = order.amount;
    if (!itemId || !orderAmount)
        return 0;

    uint32 delivered = record ? std::min(record->delivered, record->amount) : 0;
    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId) + delivered;
    if (currentCount >= orderAmount)
        return 0;

    uint32 craftSpellId = FindCraftSpellFor(bot, itemId);
    if (!craftSpellId)
        return 0;

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(craftSpellId);
    if (!pSpellInfo)
        return 0;

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
        if (TortoiseBots::CraftOrderPolicy::ReagentShortfall(
                pSpellInfo->ReagentCount[i], orderAmount - currentCount, have) == 0)
            continue;

        return reagentId;
    }

    return 0;
}

bool GuildOrderNeedsCapitalValue::Calculate()
{
    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (!order.IsCraftOrder())
        return false;

    std::optional<TortoiseBots::GuildOrders::OrderRecord> record =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (record && record->state != TortoiseBots::GuildOrders::OrderState::Working)
        return false;

    uint32 itemId = order.itemId;
    uint32 orderAmount = order.amount;
    if (!itemId || !orderAmount)
        return false;

    uint32 delivered = record ? std::min(record->delivered, record->amount) : 0;
    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId) + delivered;
    if (currentCount >= orderAmount)
        return false;

    uint32 craftSpellId = FindCraftSpellFor(bot, itemId);
    if (!craftSpellId)
        return false;

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(craftSpellId);
    if (!pSpellInfo || !pSpellInfo->RequiresSpellFocus)
        return false;

    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!pSpellInfo->ReagentCount[i] || pSpellInfo->Reagent[i] <= 0)
            continue;

        uint32 have = ai->GetInventoryItemsCountWithId(pSpellInfo->Reagent[i]);
        if (TortoiseBots::CraftOrderPolicy::ReagentShortfall(
                pSpellInfo->ReagentCount[i], orderAmount - currentCount, have) != 0)
            return false;
    }

    uint32 zone = bot->GetZoneId();
    AreaEntry const* area = AreaEntry::GetById(zone);
    return !area || !(area->Flags & AREA_FLAG_CAPITAL);
}

uint32 GuildOrderFarmItemValue::Calculate()
{
    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    if (order.type == GuildOrderType::Farm)
        return order.itemId;

    if (!order.IsCraftOrder())
        return 0;

    std::optional<TortoiseBots::GuildOrders::OrderRecord> record =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (record && record->state != TortoiseBots::GuildOrders::OrderState::Working)
        return 0;

    uint32 itemId = order.itemId;
    uint32 orderAmount = order.amount;
    if (!itemId || !orderAmount)
        return 0;

    uint32 delivered = record ? std::min(record->delivered, record->amount) : 0;
    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId) + delivered;
    if (currentCount >= orderAmount)
        return 0;

    uint32 craftSpellId = FindCraftSpellFor(bot, itemId);
    if (!craftSpellId)
        return 0;

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(craftSpellId);
    if (!pSpellInfo)
        return 0;

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
        uint32 want = TortoiseBots::CraftOrderPolicy::ReagentShortfall(
            pSpellInfo->ReagentCount[i], orderAmount - currentCount, have);
        if (!want)
            continue;

        TortoiseBots::GuildOrders::SourcePlan plan =
            ai::PlanGuildOrderSources(bot, reagentId, TortoiseBots::GuildOrders::OrderType::Farm);
        if (plan.verdict != TortoiseBots::GuildOrders::OrderReason::None)
            continue;

        return reagentId;
    }

    return 0;
}

uint8 PetitionSignsValue::Calculate()
{
    if (bot->GetGuildId())
        return 0;

    std::list<Item*> petitions = AI_VALUE2(std::list<Item*>, "inventory items", chat->formatQItem(5863));

    if (petitions.empty())
        return 0;

    auto result = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE petitionguid = '%u'", petitions.front()->getObjectGuid().GetCounter());

    return result ? (uint8)result->GetRowCount() : 0;
};

bool CanBuyTabard::Calculate()
{
	if (!bot->GetGuildId())
		return false;

	if (AI_VALUE(TravelTarget*,"travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL)
		return false;

	bool inCity = false;
	AreaTableEntry const* areaEntry = GetAreaEntryByAreaID(sServerFacade.GetAreaId(bot));
	if (areaEntry)
	{
		if (areaEntry->ZoneId)
			areaEntry = GetAreaEntryByAreaID(areaEntry->ZoneId);

		if (areaEntry && areaEntry->Flags & AREA_FLAG_CAPITAL)
			inCity = true;
	}

	if (!inCity)
		return false;

	if (AI_VALUE2(uint32, "item count", chat->formatQItem(5976)))
		return false;

	if (AI_VALUE2(uint32, "free money for", uint32(NeedMoneyFor::guild)) < 10000)
		return false;

	return true;
}
