
#include "playerbot/playerbot.h"
#include "GuildValues.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/ServerFacade.h"
#include "runtime/GuildOrderStore.h"
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
