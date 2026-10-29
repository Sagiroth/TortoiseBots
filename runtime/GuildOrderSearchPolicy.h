#pragma once

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

} // namespace GuildOrders
} // namespace TortoiseBots
