
#include "playerbot/playerbot.h"
#include "GuildValues.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/GuildOrderSources.h"
#include "playerbot/GuildOrderCraft.h"
#include "runtime/GuildOrderStore.h"
#include "runtime/GuildOrderSourcesPolicy.h"
#include "runtime/policy/provision/CraftOrderPolicy.h"
#include "Guild/GuildMgr.h"
#include "Mail/Mail.h"
#include "MapNodes/MasterPlayer.h"

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
            order.type = stored->farmRest ? GuildOrderType::Farm : GuildOrderType::AuctionHouse;
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

uint32 ai::OrderMailboxItems(Player* bot, uint32 itemId)
{
    if (!bot || !bot->GetSession() || !itemId)
        return 0;
    MasterPlayer* master = bot->GetSession()->GetMasterPlayer();
    if (!master)
        return 0;
    uint32 count = 0;
    time_t curTime = time(nullptr);
    for (PlayerMails::iterator itr = master->GetMailBegin(); itr != master->GetMailEnd(); ++itr)
    {
        Mail* mail = *itr;
        if (!mail || mail->state == MAIL_STATE_DELETED || curTime < mail->deliver_time)
            continue;
        if (!mail->has_items)
            continue;
        for (MailItemInfoVec::const_iterator itemItr = mail->items.begin(); itemItr != mail->items.end(); ++itemItr)
            if (itemItr->item_template == itemId)
            {
                Item* mailItem = master->GetMItem(itemItr->item_guid);
                count += mailItem ? mailItem->GetCount() : 1;
            }
    }
    return count;
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
    return stored->delivered + bot->GetItemCount(order.itemId) +
        OrderMailboxItems(bot, order.itemId) < order.amount;
}

bool HasGuildTravelOrderValue::Calculate()
{
    // No order work while grouped with a player, for any order type.
    if (ai->HasRealPlayerMaster())
        return false;

    GuildOrder order = AI_VALUE(GuildOrder, "guild order");
    // A craft order short on a farmable reagent travels like a farm order.
    if (order.IsCraftOrder())
        return AI_VALUE(uint32, "guild order farm item") != 0;
    if (!order.IsTravelOrder())
        return false;

    // No order work while delivering or done with the amount (delivered
    // plus bags plus mailbox). Mail waiting never blocks the leg: the
    // travel code walks to a mailbox and collects it before farming, buying
    // or delivering, since bought items arrive by mail.
    if (!OrderStillNeedsWork(bot, order))
        return false;
    return true;
}

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

    uint32 craftSpellId = ai::FindCraftSpellFor(bot, itemId);
    if (!craftSpellId)
        return 0;

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(craftSpellId);
    if (!pSpellInfo)
        return 0;

    uint32 casts = ai::CraftCastsForRemaining(craftSpellId, itemId, orderAmount - currentCount);

    // Affordable casts first: when the bags already hold a cast but cannot
    // take more of a short reagent, craft instead of asking for more.
    uint32 affordable = casts;
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!pSpellInfo->ReagentCount[i] || pSpellInfo->Reagent[i] <= 0)
            continue;

        uint32 have = ai->GetInventoryItemsCountWithId(pSpellInfo->Reagent[i]);
        affordable = std::min(affordable,
            TortoiseBots::CraftOrderPolicy::CastsAffordable(have, pSpellInfo->ReagentCount[i]));
    }

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
                pSpellInfo->ReagentCount[i], casts, have) == 0)
            continue;

        if (affordable >= 1 && AI_VALUE2(uint32, "stack space for item", reagentId) == 0)
            return 0;

        return reagentId;
    }

    return 0;
}

bool GuildOrderNeedsFocusValue::Calculate()
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

    uint32 craftSpellId = ai::FindCraftSpellFor(bot, itemId);
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
        if (TortoiseBots::CraftOrderPolicy::CastsAffordable(have, pSpellInfo->ReagentCount[i]) == 0)
            return false;
    }

    std::list<ObjectGuid> objects = AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los");
    for (ObjectGuid guid : objects)
    {
        GameObject* go = ai->GetGameObject(guid);
        if (!go || !sServerFacade.isSpawned(go))
            continue;

        GameObjectInfo const* info = sObjectMgr.GetGameObjectInfo(go->GetEntry());
        if (!info || info->type != GAMEOBJECT_TYPE_SPELL_FOCUS ||
            info->spellFocus.focusId != pSpellInfo->RequiresSpellFocus)
            continue;

        if (bot->GetDistance(go) <= sPlayerbotAIConfig.sightDistance)
            return false;
    }

    return true;
}

uint32 ai::GuildOrderCraftReagentFor(PlayerbotAI* ai, Player* bot, uint32 itemId, uint32 amount,
    uint32 delivered)
{
    if (!ai || !bot || !itemId || !amount)
        return 0;

    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId) + std::min(delivered, amount);
    if (currentCount >= amount)
        return 0;

    uint32 craftSpellId = ai::FindCraftSpellFor(bot, itemId);
    if (!craftSpellId)
        return 0;

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(craftSpellId);
    if (!pSpellInfo)
        return 0;

    uint32 casts = ai::CraftCastsForRemaining(craftSpellId, itemId, amount - currentCount);

    uint32 affordable = ai::CraftAffordableCasts(pSpellInfo, casts,
        [&](uint32 reagentId) { return ai->GetInventoryItemsCountWithId(reagentId); });

    AiObjectContext* context = ai->GetAiObjectContext();
    return ai::FirstFarmableReagentFor(pSpellInfo, casts, affordable,
        [&](uint32 reagentId) { return ai->GetInventoryItemsCountWithId(reagentId); },
        [&](uint32 reagentId)
        {
            auto* space = context->GetValue<uint32>("stack space for item", std::to_string(reagentId));
            return space ? space->Get() : 0;
        },
        [&](uint32 reagentId)
        {
            ItemPrototype const* reagentProto = sObjectMgr.GetItemPrototype(reagentId);
            return reagentProto && ItemUsageValue::IsItemSoldByAnyVendor(reagentProto) &&
                !ItemUsageValue::IsItemSoldByAnyVendorButHasLimitedMaxCount(reagentProto);
        },
        [&](uint32 reagentId)
        {
            return ai::PlanGuildOrderSources(bot, reagentId,
                TortoiseBots::GuildOrders::OrderType::Farm).verdict == TortoiseBots::GuildOrders::OrderReason::None;
        });
}

double ai::GuildOrderCraftHardestReagentKills(PlayerbotAI* ai, Player* bot, uint32 itemId, uint32 amount,
    uint32 delivered)
{
    if (!ai || !bot || !itemId || !amount)
        return -1.0;

    uint32 currentCount = ai->GetInventoryItemsCountWithId(itemId) + std::min(delivered, amount);
    if (currentCount >= amount)
        return -1.0;

    uint32 craftSpellId = ai::FindCraftSpellFor(bot, itemId);
    if (!craftSpellId)
        return -1.0;

    const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(craftSpellId);
    if (!pSpellInfo)
        return -1.0;

    uint32 casts = ai::CraftCastsForRemaining(craftSpellId, itemId, amount - currentCount);
    double hardest = -1.0;
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (!pSpellInfo->ReagentCount[i] || pSpellInfo->Reagent[i] <= 0)
            continue;
        uint32 reagentId = pSpellInfo->Reagent[i];
        uint32 need = pSpellInfo->ReagentCount[i] * casts;
        uint32 have = ai->GetInventoryItemsCountWithId(reagentId);
        if (have >= need)
            continue;
        ItemPrototype const* reagentProto = sObjectMgr.GetItemPrototype(reagentId);
        if (reagentProto && ItemUsageValue::IsItemSoldByAnyVendor(reagentProto) &&
            !ItemUsageValue::IsItemSoldByAnyVendorButHasLimitedMaxCount(reagentProto))
            continue;
        TortoiseBots::GuildOrders::SourcePlan plan =
            ai::PlanGuildOrderSources(bot, reagentId, TortoiseBots::GuildOrders::OrderType::Farm);
        if (plan.verdict != TortoiseBots::GuildOrders::OrderReason::None || plan.zones.empty())
            continue;
        hardest = std::max(hardest,
            TortoiseBots::GuildOrders::ExpectedKills(need - have, plan.bestChance));
    }
    return hardest;
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

    uint32 delivered = record ? record->delivered : 0;
    return ai::GuildOrderCraftReagentFor(ai, bot, order.itemId, order.amount, delivered);
}

bool ai::OrderNeedsDelivery(Player* bot)
{
    if (!bot || !bot->GetGuildId())
        return false;
    std::optional<TortoiseBots::GuildOrders::OrderRecord> stored =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (!stored || stored->guildId != bot->GetGuildId())
        return false;
    if (stored->state != TortoiseBots::GuildOrders::OrderState::Delivering || !stored->itemId)
        return false;
    return bot->GetItemCount(stored->itemId) > 0;
}

bool HasGuildOrderDeliveryValue::Calculate()
{
    if (ai->HasRealPlayerMaster())
        return false;
    return OrderNeedsDelivery(bot);
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
