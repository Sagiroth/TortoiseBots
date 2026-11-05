// pi-lens-ignore-file: all
// pi-lens-ignore: clang:pp_file_not_found
#include "BotCommandHandlers.h"
#include "CommandShared.h"

#include "../ai/playerbot/GuildOrderEnchants.h"
#include "../ai/playerbot/GuildOrderSources.h"
#include "../runtime/GuildOrderCategoryPolicy.h"
#include "../runtime/GuildOrderContract.h"
#include "../runtime/GuildOrderSearchPolicy.h"
#include "../runtime/GuildOrderStore.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
#include "Database/DBCStores.h"
#include "Database/SQLStorages.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_set>
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
    TortoiseBots::GuildOrders::OrderItemFacts facts;
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

// Items a primary or secondary profession recipe creates. Class skills
// (a warlock's Healthstone, a mage's conjured food) are not crafting.
std::unordered_set<uint32_t> ProfessionCraftedItems()
{
    std::unordered_set<uint32_t> items;
    for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
    {
        SkillLineAbilityEntry const* ability = sSkillLineAbilityStore.LookupEntry(i);
        SkillLineEntry const* skill = ability ? sSkillLineStore.LookupEntry(ability->skillId) : nullptr;
        if (!skill || (skill->categoryId != SKILL_CATEGORY_PROFESSION && skill->categoryId != SKILL_CATEGORY_SECONDARY))
            continue;
        SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(ability->spellId);
        if (!spell)
            continue;
        for (int e = 0; e < 3; ++e)
            if (spell->Effect[e] == SPELL_EFFECT_CREATE_ITEM && spell->EffectItemType[e])
                items.insert(spell->EffectItemType[e]);
    }
    return items;
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

// `.bot order search <BotName> enchant <text>`: the permanent enchants
// this bot knows whose catalogue name or slot matches. One
// TBM:ORDER_ENCHANT row each, verdict ok or noskill; unknown enchants never
// list. Reagents count bags plus mailbox, like the craft search. Sorted by
// required skill, then name.
bool HandleOrderEnchantSearch(ChatHandler* handler, Player* bot, std::string const& text)
{
    std::string query = Trim(text);
    if (query.size() < kMinSearchChars)
    {
        SendActionError(handler, "order", "invalid", "Search text needs at least 3 characters.");
        return true;
    }
    if (!ai::GuildOrderEnchantsReady())
    {
        SendActionError(handler, "order", "disabled", "Enchant catalogue is not ready yet.");
        return true;
    }
    std::vector<std::string> aliased;
    for (std::string const& word : SplitQueryWords(ToLower(query)))
        aliased.push_back(TortoiseBots::GuildOrders::EnchantSlotAlias(word));
    std::vector<EnchantSnapshot> snapshots = OrderableEnchantSpells(bot, aliased);
    struct EnchantRow
    {
        TortoiseBots::GuildOrders::EnchantEntry entry;
        TortoiseBots::GuildOrders::OrderReason verdict = TortoiseBots::GuildOrders::OrderReason::None;
        std::vector<CraftRecipeReagent> reagents;
    };
    std::vector<EnchantRow> rows;
    rows.reserve(snapshots.size());
    for (EnchantSnapshot const& snapshot : snapshots)
    {
        TortoiseBots::GuildOrders::EnchantEntry entry;
        for (TortoiseBots::GuildOrders::EnchantEntry const& catalogue : ai::GuildOrderEnchants())
            if (catalogue.spellId == snapshot.spellId)
            {
                entry = catalogue;
                break;
            }
        if (!entry.spellId)
            continue;
        EnchantRow row;
        row.entry = entry;
        row.verdict = TortoiseBots::GuildOrders::EnchantSearchVerdict(true, snapshot.skillValue,
            snapshot.reqSkillValue);
        row.reagents = snapshot.reagents;
        rows.push_back(std::move(row));
    }
    std::sort(rows.begin(), rows.end(), [](EnchantRow const& a, EnchantRow const& b)
    {
        return TortoiseBots::GuildOrders::EnchantSortLess(a.entry, b.entry);
    });
    if (rows.size() > kMaxSearchResults)
        rows.resize(kMaxSearchResults);
    handler->PSendSysMessage("%s",
        TortoiseBots::GuildOrders::FormatEnchantsBegin(uint32_t(rows.size())).c_str());
    for (EnchantRow const& row : rows)
    {
        std::vector<TortoiseBots::GuildOrders::EnchantSearchReagent> reagents;
        reagents.reserve(row.reagents.size());
        for (CraftRecipeReagent const& reagent : row.reagents)
            reagents.push_back({ reagent.itemId, reagent.need, reagent.have });
        std::string const reagentsToken = TortoiseBots::GuildOrders::FormatEnchantReagents(
            reagents.empty() ? nullptr : reagents.data(), reagents.size());
        handler->PSendSysMessage("%s",
            TortoiseBots::GuildOrders::FormatEnchantRow(row.entry.spellId,
                ProtocolSafe(row.entry.name), row.entry.slot, row.entry.reqSkill, row.verdict,
                reagentsToken).c_str());
    }
    handler->PSendSysMessage("%s", TortoiseBots::GuildOrders::FormatEnchantsEnd().c_str());
    return true;
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
        SendActionError(handler, "order", "invalid", "Usage: .bot order search <bot> <farm|buy|craft|enchant> [cat=<kind>] <text>");
        return true;
    }
    if (orderType == OrderType::Enchant)
        return HandleOrderEnchantSearch(handler, bot, text);

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
            std::unordered_set<uint32_t> const crafted = ProfessionCraftedItems();
            // The item map, not the 0..max-entry range: transmog clones
            // push the largest id far past the real templates.
            for (auto const& [itemId, proto] : sObjectMgr.GetItemPrototypeMap())
            {
                if (!TortoiseBots::GuildOrders::IsOrderableItem(&proto) || (proto.Flags & ITEM_FLAG_CONJURED))
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
                row.facts.material = proto.Class == ITEM_CLASS_TRADE_GOODS || proto.Class == ITEM_CLASS_REAGENT;
                row.facts.craftable = crafted.count(itemId) != 0;
                row.facts.sources = flags;
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
            if (!TortoiseBots::GuildOrders::OrderSearchShows(orderType, row.facts))
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
