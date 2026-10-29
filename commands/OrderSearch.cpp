// pi-lens-ignore-file: all
// pi-lens-ignore: clang:pp_file_not_found
#include "BotCommandHandlers.h"
#include "CommandShared.h"

#include "../ai/playerbot/GuildOrderSources.h"
#include "../runtime/GuildOrderContract.h"
#include "../runtime/GuildOrderStore.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace TortoiseBots {
namespace BotCommands {
namespace {

using TortoiseBots::GuildOrders::OrderReason;
using TortoiseBots::GuildOrders::OrderType;
using TortoiseBots::GuildOrders::ReasonToken;
using TortoiseBots::GuildOrders::SourcesToken;

constexpr size_t kMinSearchChars = 3;
constexpr size_t kMaxSearchResults = 30;

struct SearchRow
{
    uint32_t itemId = 0;
    std::string name;
    std::string lower;
};

std::mutex& SearchIndexLock()
{
    static std::mutex lock;
    return lock;
}

std::vector<SearchRow>& SearchIndex()
{
    static std::vector<SearchRow> index;
    return index;
}

bool& SearchIndexBuilt()
{
    static bool built = false;
    return built;
}

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}

std::string ZoneName(uint32_t zoneId)
{
    if (!zoneId)
        return "-";
    AreaTableEntry const* area = GetAreaEntryByAreaID(zoneId);
    if (!area || !area->Name)
        return "-";
    return area->Name;
}

} // namespace

bool HandleOrderSearch(ChatHandler* handler, Player* requester, Player* bot, std::string const& type,
                       std::string const& text)
{
    if (!handler || !requester || !bot)
        return false;

    OrderType orderType = OrderType::Farm;
    if (!ParseType(type, orderType))
    {
        SendActionError(handler, "order", "invalid", "Usage: .bot order search <bot> <farm|buy|craft> <text>");
        return true;
    }

    std::string query = Trim(text);
    if (query.size() < kMinSearchChars)
    {
        SendActionError(handler, "order", "invalid", "Search text needs at least 3 characters.");
        return true;
    }

    {
        std::lock_guard<std::mutex> guard(SearchIndexLock());
        if (!SearchIndexBuilt())
        {
            for (uint32_t itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
            {
                ItemPrototype const* proto = sItemStorage.LookupEntry<ItemPrototype>(itemId);
                if (!TortoiseBots::GuildOrders::IsOrderableItem(proto))
                    continue;
                SearchRow row;
                row.itemId = itemId;
                row.name = proto->Name1;
                row.lower = ToLower(proto->Name1);
                // Developer test items ("Test AQ Resource - ...") only add noise.
                if (row.lower.rfind("test ", 0) == 0)
                    continue;
                SearchIndex().push_back(std::move(row));
            }
            SearchIndexBuilt() = true;
        }
    }

    std::string const needle = ToLower(query);
    std::vector<SearchRow const*> matches;
    {
        std::lock_guard<std::mutex> guard(SearchIndexLock());
        for (SearchRow const& row : SearchIndex())
        {
            if (matches.size() >= kMaxSearchResults)
                break;
            if (row.lower.find(needle) != std::string::npos)
                matches.push_back(&row);
        }
    }

    handler->PSendSysMessage("TBM:ORDER_ITEMS_BEGIN|%u", static_cast<uint32_t>(matches.size()));
    for (SearchRow const* row : matches)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(row->itemId);
        if (!proto)
            continue;
        TortoiseBots::GuildOrders::SourcePlan plan = ai::PlanGuildOrderSources(bot, row->itemId, orderType);
        std::string const zone = plan.zones.empty() ? "-" : ZoneName(plan.zones.front().zoneId);
        uint32_t const chance = plan.zones.empty() ? 0 : plan.zones.front().chance;
        std::string const skill = plan.skillId ? std::to_string(plan.skillId) + ":" + std::to_string(plan.skillValue) : "-";
        std::string const verdict = plan.verdict == OrderReason::None ? "ok" : ReasonToken(plan.verdict);
        handler->PSendSysMessage("TBM:ORDER_ITEM|%u|%s|%u|%s|%u|%s|%s|%s|%u",
            row->itemId, ProtocolSafe(row->name).c_str(), uint32_t(proto->Quality),
            SourcesToken(plan.sources).c_str(), plan.minLevel, skill.c_str(), verdict.c_str(),
            ProtocolSafe(zone).c_str(), chance);
    }
    handler->PSendSysMessage("TBM:ORDER_ITEMS_END");
    return true;
}

} // namespace BotCommands
} // namespace TortoiseBots
