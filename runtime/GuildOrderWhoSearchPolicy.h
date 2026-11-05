#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// Guild-wide "who can make this" search: pure argument, sort, count and
// line-format rules (commands/OrderWhoSearch.cpp feeds them from the live
// guild roster and the shared profession recipe data). Header-only and free
// of core types so the rules stay unit tested on their own
// (tools/test_guild_order_who_search_policy.cpp). Names cross the boundary
// already pipe-sanitized; the formatters below never escape.

namespace TortoiseBots
{
namespace GuildOrders
{

// A first search argument that names no bot: one of farm|buy|craft|enchant
// (case-insensitive). A bot literally named like one keeps no special
// handling beyond this check.
inline bool IsNoBotFirstArg(std::string const& token)
{
    std::string lower = token;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return char(std::tolower(c)); });
    return lower == "farm" || lower == "buy" || lower == "craft" || lower == "enchant";
}

// One bot behind a match: its profession skill, how many distinct reagents
// it already holds in full, how many distinct reagents the recipe needs,
// and whether it already runs an order.
struct WhoBot
{
    std::string name;
    uint32_t skill = 0;
    uint32_t have = 0;
    uint32_t need = 0;
    bool busy = false;
};

// Bots sort by profession skill high to low, name breaking ties so the
// reply order is stable.
inline void SortWhoBots(std::vector<WhoBot>& bots)
{
    std::sort(bots.begin(), bots.end(), [](WhoBot const& a, WhoBot const& b)
    {
        if (a.skill != b.skill)
            return a.skill > b.skill;
        return a.name < b.name;
    });
}

// One distinct reagent of a recipe: the count one cast needs and the count
// the bot holds. Callers only pass real reagent rows (need > 0).
struct WhoReagent
{
    uint32_t need = 0;
    uint32_t have = 0;
};

// have = rows the bot holds in full (have >= need), need = rows total.
inline void CountWhoReagents(std::vector<WhoReagent> const& reagents, uint32_t& have, uint32_t& need)
{
    have = 0;
    need = static_cast<uint32_t>(reagents.size());
    for (WhoReagent const& reagent : reagents)
        if (reagent.have >= reagent.need)
            ++have;
}

inline std::string FormatWhoBegin(std::string const& kind, uint32_t count)
{
    return "TBM:ORDER_WHO_BEGIN|" + kind + "|" + std::to_string(count);
}

inline std::string FormatWhoEnd()
{
    return "TBM:ORDER_WHO_END";
}

inline std::string FormatWhoBots(std::vector<WhoBot> const& bots)
{
    if (bots.empty())
        return "-";
    std::string out;
    for (WhoBot const& bot : bots)
    {
        if (!out.empty())
            out += ';';
        out += bot.name + ":" + std::to_string(bot.skill) + ":" + std::to_string(bot.have) + ":" +
               std::to_string(bot.need) + ":" + (bot.busy ? "1" : "0");
    }
    return out;
}

// One match row. Craft passes id = crafted itemId, quality = item quality,
// slot = "-". Enchant later reuses the same shape with id = spellId,
// quality = "-", slot = slot token.
inline std::string FormatWhoRow(std::string const& kind, uint32_t id, std::string const& name,
    std::string const& quality, std::string const& slot, uint32_t reqSkill, std::string const& bots)
{
    return "TBM:ORDER_WHO|" + kind + "|" + std::to_string(id) + "|" + name + "|" + quality + "|" +
           slot + "|" + std::to_string(reqSkill) + "|" + bots;
}

} // namespace GuildOrders
} // namespace TortoiseBots
