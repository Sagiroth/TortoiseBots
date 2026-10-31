// pi-lens-ignore-file: all
// pi-lens-ignore: clang:pp_file_not_found
#include "BotCommandHandlers.h"
#include "CommandShared.h"

#include "../ai/playerbot/GuildOrderSources.h"
#include "../runtime/GuildOrderCategoryPolicy.h"
#include "../runtime/GuildOrderContract.h"
#include "../runtime/GuildOrderSearchPolicy.h"
#include "../runtime/GuildOrderStore.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <vector>

namespace TortoiseBots {
namespace BotCommands {
namespace {

using TortoiseBots::GuildOrders::CategoryArgParse;
using TortoiseBots::GuildOrders::OrderReason;
using TortoiseBots::GuildOrders::OrderType;
using TortoiseBots::GuildOrders::ReasonToken;
using TortoiseBots::GuildOrders::SourcesToken;
using TortoiseBots::GuildOrders::CategoryToken;
using TortoiseBots::GuildOrders::ClassifyOrderCategory;
using TortoiseBots::GuildOrders::IsTradeCloth;
using TortoiseBots::GuildOrders::NormalizeQueryWords;
using TortoiseBots::GuildOrders::OrderItemCategory;
using TortoiseBots::GuildOrders::SearchNameMatches;
using TortoiseBots::GuildOrders::SearchNameRank;
using TortoiseBots::GuildOrders::SplitQueryWords;
using TortoiseBots::GuildOrders::StripCategoryArg;
constexpr size_t kMinSearchChars = 3;
constexpr size_t kMaxSearchResults = 30;
struct SearchRow
{
    uint32_t itemId = 0;
    std::string name;
    std::string lower;
    OrderItemCategory category = OrderItemCategory::Other;
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

// Only the small gather kinds browse without text: drop and other hold
// thousands of items, and the 30-row cut would show the same "A" slice
// while the planner never sees the rest.
bool IsBrowsableCategory(OrderItemCategory category)
{
    return category == OrderItemCategory::Herb || category == OrderItemCategory::Ore ||
           category == OrderItemCategory::Leather || category == OrderItemCategory::Fish ||
           category == OrderItemCategory::Cloth;
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
        SendActionError(handler, "order", "invalid", "Usage: .bot order search <bot> <farm|buy|craft> [cat=<kind>] <text>");
        return true;
    }

    std::string query = Trim(text);
    OrderItemCategory category = OrderItemCategory::Other;
    CategoryArgParse const catParse = StripCategoryArg(query, category);
    if (catParse == CategoryArgParse::Bad)
    {
        SendActionError(handler, "order", "invalid",
            "Unknown kind. Use one of: herb, ore, leather, fish, cloth, drop, other.");
        return true;
    }
    bool const hasCategory = catParse == CategoryArgParse::Ok;
    if (query.empty())
    {
        // drop and other hold thousands of items: browsing them would show
        // the same 30 alphabetical rows while the planner never sees the
        // rest, so those kinds need search text.
        if (hasCategory && !IsBrowsableCategory(category))
        {
            SendActionError(handler, "order", "invalid",
                "That kind is too big to browse. Add at least 3 characters of search text.");
            return true;
        }
    }
    else if (query.size() < kMinSearchChars)
    {
        SendActionError(handler, "order", "invalid", "Search text needs at least 3 characters.");
        return true;
    }

    // The dispatch gates every search on GuildOrderSourcesReady, so the
    // source flags below are final; still, an index built while sources
    // are not ready must never be cached, or every kind freezes as Other.
    bool const sourcesReady = ai::GuildOrderSourcesReady();
    {
        std::lock_guard<std::mutex> guard(SearchIndexLock());
        if (!SearchIndexBuilt())
        {
            // The item map, not the 0..max-entry range: transmog clones
            // push the largest id far past the real templates.
            for (auto const& [itemId, proto] : sObjectMgr.GetItemPrototypeMap())
            {
                if (!TortoiseBots::GuildOrders::IsOrderableItem(&proto))
                    continue;
                SearchRow row;
                row.itemId = itemId;
                row.name = proto.Name1;
                row.lower = ToLower(proto.Name1);
                // Developer test items ("Test AQ Resource - ...") only add noise.
                if (row.lower.rfind("test ", 0) == 0)
                    continue;
                uint8_t flags = 0;
                for (ai::GuildOrderEntrySource const& source : ai::GuildOrderSourcesFor(itemId))
                    flags |= source.flag;
                row.category = ClassifyOrderCategory(SourcesToken(flags), IsTradeCloth(proto.Class, row.lower));
                SearchIndex().push_back(std::move(row));
            }
            if (sourcesReady)
                SearchIndexBuilt() = true;
        }
    }

    std::vector<std::string> const words = SplitQueryWords(ToLower(query));
    std::string const needle = NormalizeQueryWords(words);
    struct ScoredRow
    {
        SearchRow const* row = nullptr;
        uint32_t rank = 3;
    };
    std::vector<ScoredRow> scored;
    {
        std::lock_guard<std::mutex> guard(SearchIndexLock());
        for (SearchRow const& row : SearchIndex())
        {
            if (hasCategory && row.category != category)
                continue;
            if (!words.empty() && !SearchNameMatches(row.lower, words))
                continue;
            scored.push_back({ &row, words.empty() ? 1 : SearchNameRank(row.lower, needle) });
        }
    }
    // Name rank first, then the bot's verdict inside each rank (planner runs
    // only for the rows that survive the cut), then the name.
    std::sort(scored.begin(), scored.end(), [](ScoredRow const& a, ScoredRow const& b)
    {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        return a.row->name < b.row->name;
    });
    if (scored.size() > kMaxSearchResults)
        scored.resize(kMaxSearchResults);

    struct ResultRow
    {
        SearchRow const* row = nullptr;
        uint32_t rank = 3;
        bool usable = false;
        TortoiseBots::GuildOrders::SourcePlan plan;
        std::string difficulty;
    };
    std::vector<ResultRow> results;
    results.reserve(scored.size());
    for (ScoredRow const& scoredRow : scored)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(scoredRow.row->itemId);
        if (!proto)
            continue;
        ResultRow result;
        result.row = scoredRow.row;
        result.rank = scoredRow.rank;
        result.plan = ai::PlanGuildOrderSources(bot, scoredRow.row->itemId, orderType);
        result.usable = result.plan.verdict == OrderReason::None;
        result.difficulty = ai::PlanOrderDifficulty(bot, scoredRow.row->itemId, orderType, 1);
        results.push_back(std::move(result));
    }
    std::sort(results.begin(), results.end(), [](ResultRow const& a, ResultRow const& b)
    {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        if (a.usable != b.usable)
            return a.usable;
        return a.row->name < b.row->name;
    });

    handler->PSendSysMessage("TBM:ORDER_ITEMS_BEGIN|%u", static_cast<uint32_t>(results.size()));
    for (ResultRow const& result : results)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(result.row->itemId);
        if (!proto)
            continue;
        TortoiseBots::GuildOrders::SourcePlan const& plan = result.plan;
        std::string const zone = plan.zones.empty() ? "-" : ZoneName(plan.zones.front().zoneId);
        uint32_t const chance = plan.zones.empty() ? 0 : plan.zones.front().chance;
        std::string const skill = plan.skillId ? std::to_string(plan.skillId) + ":" + std::to_string(plan.skillValue) : "-";
        std::string const verdict = plan.verdict == OrderReason::None ? "ok" : ReasonToken(plan.verdict);
        handler->PSendSysMessage("TBM:ORDER_ITEM|%u|%s|%u|%s|%u|%s|%s|%s|%u|%s|%s",
            result.row->itemId, ProtocolSafe(result.row->name).c_str(), uint32_t(proto->Quality),
            SourcesToken(plan.sources).c_str(), plan.minLevel, skill.c_str(), verdict.c_str(),
            ProtocolSafe(zone).c_str(), chance, result.difficulty.c_str(), CategoryToken(result.row->category));
    }
    handler->PSendSysMessage("TBM:ORDER_ITEMS_END");
    return true;
}

} // namespace BotCommands
} // namespace TortoiseBots
