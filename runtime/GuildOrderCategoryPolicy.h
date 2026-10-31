#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>
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

// Item class 7 is Trade Goods (core ITEM_CLASS_TRADE_GOODS, kept literal
// so this header stays free of core types).
inline constexpr uint8_t kItemClassTradeGoods = 7;

// Trade-goods cloth by class and name: in 1.12 every trade good has
// subclass 0, so only the name tells cloth (Linen Cloth to Mooncloth,
// and their bolts) from ore or meat. lowerName is the lowercased item
// name; armor called "cloth" fails on the class.
inline bool IsTradeCloth(uint8_t itemClass, std::string const& lowerName)
{
    if (itemClass != kItemClassTradeGoods || lowerName.size() < 5)
        return false;
    return lowerName.compare(lowerName.size() - 5, 5, "cloth") == 0;
}

// Kind of one item: Trade Goods cloth by class and name first (in 1.12 a
// cloth drop can also skin, e.g. Wool Cloth from Sheep: it stays cloth),
// then the planner's source tokens (herb, ore, skinning, fishing), then a
// plain creature drop, else other. sourcesToken is the comma list from
// SourcesToken; isCloth is IsTradeCloth off the prototype.
inline OrderItemCategory ClassifyOrderCategory(std::string const& sourcesToken, bool isCloth)
{
    if (isCloth)
        return OrderItemCategory::Cloth;
    if (SourcesTokenHas(sourcesToken, "herb"))
        return OrderItemCategory::Herb;
    if (SourcesTokenHas(sourcesToken, "ore"))
        return OrderItemCategory::Ore;
    if (SourcesTokenHas(sourcesToken, "skin"))
        return OrderItemCategory::Leather;
    if (SourcesTokenHas(sourcesToken, "fish"))
        return OrderItemCategory::Fish;
    if (SourcesTokenHas(sourcesToken, "drop"))
        return OrderItemCategory::Drop;
    return OrderItemCategory::Other;
}

enum class CategoryArgParse : uint8_t { None, Ok, Bad };

// Pulls a "cat=<kind>" word out of the search text, wherever it sits:
// "linen cat=cloth" and "cat=cloth linen" both browse cloth for linen,
// and "cat= cloth" takes the kind from the next word. The "cat=" match
// is case-insensitive. None when no cat= word is present, Bad when the
// kind is unknown (query untouched); else Ok with query holding the
// remaining words, rejoined with single spaces. No item name contains
// "=", so a cat= word is never search text.
inline CategoryArgParse StripCategoryArg(std::string& query, OrderItemCategory& out)
{
    auto isCatWord = [](std::string const& word)
    {
        return word.size() >= 4 && (word[0] == 'c' || word[0] == 'C') &&
               (word[1] == 'a' || word[1] == 'A') && (word[2] == 't' || word[2] == 'T') &&
               word[3] == '=';
    };
    std::vector<std::string> words;
    std::string word;
    for (char c : query)
    {
        if (c == ' ' || c == '\t')
        {
            if (!word.empty())
            {
                words.push_back(word);
                word.clear();
            }
            continue;
        }
        word += c;
    }
    if (!word.empty())
        words.push_back(word);

    std::string rest;
    bool found = false;
    bool ok = false;
    for (std::size_t i = 0; i < words.size(); ++i)
    {
        if (!isCatWord(words[i]))
        {
            if (!rest.empty())
                rest += ' ';
            rest += words[i];
            continue;
        }
        if (found)
            continue;
        found = true;
        std::string kind = words[i].size() > 4 ? words[i].substr(4) : std::string();
        if (kind.empty() && i + 1 < words.size())
            kind = words[++i];
        for (char& c : kind)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        ok = ParseCategory(kind, out);
    }
    if (!found)
        return CategoryArgParse::None;
    if (!ok)
        return CategoryArgParse::Bad;
    query = rest;
    return CategoryArgParse::Ok;
}

} // namespace GuildOrders
} // namespace TortoiseBots
