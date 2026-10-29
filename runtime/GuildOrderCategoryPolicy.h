#pragma once

#include <cstdint>
#include <string>

namespace TortoiseBots
{
namespace GuildOrders
{

// Item kind for guild-order search browsing (TBM:ORDER_ITEM field 12).
// Stable per item: it derives from the ungated order-source index, never
// from one bot's gated plan, so browsing by kind is the same for every bot.
// Header-only and free of core types so the rules stay unit tested on their
// own (tools/test_guild_order_category_policy.cpp).
enum class OrderItemCategory : uint8_t { Herb, Ore, Leather, Fish, Cloth, Drop, Other };

inline char const* CategoryToken(OrderItemCategory category)
{
    switch (category)
    {
        case OrderItemCategory::Herb:    return "herb";
        case OrderItemCategory::Ore:     return "ore";
        case OrderItemCategory::Leather: return "leather";
        case OrderItemCategory::Fish:    return "fish";
        case OrderItemCategory::Cloth:   return "cloth";
        case OrderItemCategory::Drop:    return "drop";
        case OrderItemCategory::Other:   return "other";
    }
    return "other";
}

inline bool ParseCategory(std::string const& token, OrderItemCategory& out)
{
    for (uint8_t i = 0; i <= uint8_t(OrderItemCategory::Other); ++i)
        if (token == CategoryToken(OrderItemCategory(i)))
        {
            out = OrderItemCategory(i);
            return true;
        }
    return false;
}

// Whether a comma-separated planner sources token ("drop,skin") carries one
// entry. Exact pieces only, so "herbs" never matches "herb".
inline bool SourcesTokenHas(std::string const& sources, char const* token)
{
    std::string const want(token);
    std::string::size_type start = 0;
    while (start <= sources.size())
    {
        std::string::size_type comma = sources.find(',', start);
        std::string::size_type length =
            comma == std::string::npos ? std::string::npos : comma - start;
        if (sources.compare(start, length, want) == 0)
            return true;
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return false;
}

// Kind of one item: the planner's source tokens first (herb, ore, skinning,
// fishing), then Trade Goods cloth by class, then a plain creature drop,
// else other. sourcesToken is the comma list from SourcesToken; isCloth is
// Class == Trade Goods and SubClass == cloth, read off the prototype by the
// caller. The fish token has no planner source yet; matching it by token
// keeps this working unchanged once fishing sources exist.
inline OrderItemCategory ClassifyOrderCategory(std::string const& sourcesToken, bool isCloth)
{
    if (SourcesTokenHas(sourcesToken, "herb"))
        return OrderItemCategory::Herb;
    if (SourcesTokenHas(sourcesToken, "ore"))
        return OrderItemCategory::Ore;
    if (SourcesTokenHas(sourcesToken, "skin"))
        return OrderItemCategory::Leather;
    if (SourcesTokenHas(sourcesToken, "fish"))
        return OrderItemCategory::Fish;
    if (isCloth)
        return OrderItemCategory::Cloth;
    if (SourcesTokenHas(sourcesToken, "drop"))
        return OrderItemCategory::Drop;
    return OrderItemCategory::Other;
}

} // namespace GuildOrders
} // namespace TortoiseBots
