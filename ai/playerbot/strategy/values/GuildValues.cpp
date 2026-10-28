
#include "playerbot/playerbot.h"
#include "GuildValues.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/GuildOrderSources.h"
#include "../../../../runtime/GuildOrderStore.h"
#include "../../../../runtime/policy/provision/CraftOrderPolicy.h"
#include "Guild/GuildMgr.h"

using namespace ai;

std::string GuildOrderValue::TrimWhitespace(const std::string& str)
{
    std::string result = str;
    result.erase(result.begin(), std::find_if(result.begin(), result.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    result.erase(std::find_if(result.rbegin(), result.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), result.end());
    return result;
}

bool GuildOrderValue::ParseOrderPrefix(const std::string& note, const std::string& prefix, std::string& outBody)
{
    auto pos = note.find(prefix);
    if (pos == std::string::npos)
        return false;

    std::string body = TrimWhitespace(note.substr(pos + prefix.size()));
    if (body.empty())
        return false;

    outBody = body;
    return true;
}

char* strstri(const char* haystack, const char* needle);

uint32 GuildOrderValue::FindItemByName(const std::string& name)
{
    static std::unordered_map<std::string, uint32> s_cache;

    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return std::tolower(c); });

    auto it = s_cache.find(lowerName);
    if (it != s_cache.end())
        return it->second;

    uint32 substringMatch = 0;

    for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
    {
        ItemPrototype const* proto = sItemStorage.LookupEntry<ItemPrototype>(itemId);
        if (!proto)
            continue;

        if (proto->Class == ITEM_CLASS_RECIPE ||
            proto->Class == ITEM_CLASS_WEAPON ||
            proto->Class == ITEM_CLASS_ARMOR)
            continue;

        if (name.size() == proto->Name1.size() && strstri(proto->Name1, name.c_str()))
        {
            s_cache[lowerName] = itemId;
            return itemId;
        }

        if (!substringMatch && strstri(proto->Name1, name.c_str()))
            substringMatch = itemId;
    }

    s_cache[lowerName] = substringMatch;
    return substringMatch;
}

GuildOrder GuildOrderValue::Calculate()
{
    GuildOrder order;

    if (!bot->GetGuildId())
        return order;

    Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
    if (!guild)
        return order;

    MemberSlot* member = guild->GetMemberSlot(bot->getObjectGuid());
    if (!member)
        return order;

    std::string note = member->OfficerNote;

    if (!note.empty())
    {
        std::string lower = note;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        std::string trimmed = TrimWhitespace(lower);
        if (trimmed == "skip order")
            return order;
    }

    if (note.empty())
        return order;

    std::string body;

    if (ParseOrderPrefix(note, "Craft:", body))
    {
        order.type = GuildOrderType::Craft;

        // Parse optional amount for craft orders:
        // Craft: <item> <amount>
        auto lastSpace = body.rfind(' ');
        if (lastSpace != std::string::npos)
        {
            std::string lastToken = body.substr(lastSpace + 1);
            bool isNumber = !lastToken.empty() && std::all_of(lastToken.begin(), lastToken.end(), ::isdigit);

            if (isNumber)
            {
                order.amount = std::stoul(lastToken);
                order.target = TrimWhitespace(body.substr(0, lastSpace));
                return order;
            }
        }

        order.target = body;
        order.amount = 1;
    }
    else if (ParseOrderPrefix(note, "Farm:", body))
    {
        order.type = GuildOrderType::Farm;

        // Parse optional amount for farm orders:
        // Farm: <item> <amount>
        auto lastSpace = body.rfind(' ');
        if (lastSpace != std::string::npos)
        {
            std::string lastToken = body.substr(lastSpace + 1);
            bool isNumber = !lastToken.empty() && std::all_of(lastToken.begin(), lastToken.end(), ::isdigit);

            if (isNumber)
            {
                order.amount = std::stoul(lastToken);
                order.target = TrimWhitespace(body.substr(0, lastSpace));
                return order;
            }
        }

        order.target = body;
    }

    return order;
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

    uint32 itemId = record ? record->itemId : GuildOrderValue::FindItemByName(order.target);
    uint32 orderAmount = record ? record->amount : order.amount;
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

    uint32 itemId = record ? record->itemId : GuildOrderValue::FindItemByName(order.target);
    uint32 orderAmount = record ? record->amount : order.amount;
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
    {
        std::optional<TortoiseBots::GuildOrders::OrderRecord> record =
            TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
        if (record && record->itemId)
            return record->itemId;
        return GuildOrderValue::FindItemByName(order.target);
    }

    if (!order.IsCraftOrder())
        return 0;

    std::optional<TortoiseBots::GuildOrders::OrderRecord> record =
        TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
    if (record && record->state != TortoiseBots::GuildOrders::OrderState::Working)
        return 0;

    uint32 itemId = record ? record->itemId : GuildOrderValue::FindItemByName(order.target);
    uint32 orderAmount = record ? record->amount : order.amount;
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
