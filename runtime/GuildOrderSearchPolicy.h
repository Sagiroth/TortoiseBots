#pragma once

#include "GuildOrderContract.h"

#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// Smart search over the order item index, header-only and free of core
// types so the rules stay unit tested on their own
// (tools/test_guild_order_search_policy.cpp). All matching reads lowercased
// names; SplitQueryWords skips extra spaces so "  heavy   leather " still
// searches for both words.
inline std::vector<std::string> SplitQueryWords(std::string const& query)
{
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
    return words;
}

// One spaced string from the words: ranking then sees the query the
// same way matching did, so "heavy   leather" still ranks exact.
inline std::string NormalizeQueryWords(std::vector<std::string> const& words)
{
    std::string out;
    for (std::string const& word : words)
    {
        if (!out.empty())
            out += ' ';
        out += word;
    }
    return out;
}

// Every word of the query must match somewhere in the name, in any order:
// "leather heavy" finds Heavy Leather, "leather heav" does not force word
// boundaries but keeps prefix order inside each word.
inline bool SearchNameMatches(std::string const& lowerName, std::vector<std::string> const& words)
{
    for (std::string const& word : words)
        if (lowerName.find(word) == std::string::npos)
            return false;
    return true;
}

// Match ranks, best first: the whole name, then the name starts with the
// query, then a word of the name starts with it, then it only appears
// inside a word. "Heavy Leather" for "heavy leather" is exact;
// "Heavy" for "heavy l" wins on name start over "Rough Heavy Leather" only
// on word start; "Leather" for "eather" contains.
inline uint32_t SearchNameRank(std::string const& lowerName, std::string const& query)
{
    if (lowerName == query)
        return 0;
    if (lowerName.compare(0, query.size(), query) == 0)
        return 1;
    std::size_t pos = 0;
    while (pos <= lowerName.size())
    {
        std::size_t wordStart = lowerName.find_first_not_of(' ', pos);
        if (wordStart == std::string::npos)
            break;
        if (lowerName.compare(wordStart, query.size(), query) == 0)
            return 2;
        std::size_t wordEnd = lowerName.find(' ', wordStart);
        pos = wordEnd == std::string::npos ? lowerName.size() + 1 : wordEnd + 1;
    }
    return 3;
}

// What the search knows about an item to decide whether a type shows it.
// material: Trade Goods or Reagent class. craftable: a profession recipe
// (primary or secondary skill) creates it. sources: SourceFlag bits.
struct OrderItemFacts
{
    bool material = false;
    bool craftable = false;
    uint8_t sources = 0;
};

// Farm shows what a bot can gather, or a material a mob drops; never a
// crafted item or random-drop gear. Craft shows only recipe items. Buy
// shows materials and crafted items, not random-drop gear or recipes.
inline bool OrderSearchShows(OrderType type, OrderItemFacts const& item)
{
    uint8_t const gathered = SourceHerb | SourceOre | SourceSkin | SourceFish;
    switch (type)
    {
        case OrderType::Farm:
            return (item.sources & gathered) || (item.material && (item.sources & SourceDrop));
        case OrderType::Craft:
            return item.craftable;
        case OrderType::Buy:
            return item.material || item.craftable || (item.sources & gathered);
    }
    return false;
}

} // namespace GuildOrders
} // namespace TortoiseBots
