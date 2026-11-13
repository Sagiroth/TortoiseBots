#pragma once
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Pure words and keys for `.bot blessing`: per paladin, the blessing each
// class (and optional per-character exception) gets ("auto" or unset keeps the
// automatic coverage pick). No core includes, so
// tools/test_blessing_order_policy.cpp can run it.

namespace ai
{
constexpr int kBlessingOrderClassCount = 9;
constexpr int kBlessingOrderBlessingCount = 6;
constexpr std::size_t kBlessingOrderPlayerCap = 20;

// Class words in wire order, with their CLASS_* ids.
inline char const* BlessingOrderClassWord(int index)
{
    static char const* const words[kBlessingOrderClassCount] = {
        "warrior", "paladin", "hunter", "rogue", "priest", "shaman", "mage", "warlock", "druid" };
    return index >= 0 && index < kBlessingOrderClassCount ? words[index] : "";
}

inline std::uint8_t BlessingOrderClassId(int index)
{
    static std::uint8_t const ids[kBlessingOrderClassCount] = { 1, 2, 3, 4, 5, 7, 8, 9, 11 };
    return index >= 0 && index < kBlessingOrderClassCount ? ids[index] : 0;
}

inline int BlessingOrderClassIndex(std::string const& word)
{
    for (int i = 0; i < kBlessingOrderClassCount; ++i)
        if (word == BlessingOrderClassWord(i))
            return i;
    return -1;
}

inline int BlessingOrderClassIndexById(std::uint8_t classId)
{
    for (int i = 0; i < kBlessingOrderClassCount; ++i)
        if (classId == BlessingOrderClassId(i))
            return i;
    return -1;
}

inline char const* BlessingOrderWord(int index)
{
    static char const* const words[kBlessingOrderBlessingCount] = {
        "might", "wisdom", "kings", "salvation", "sanctuary", "light" };
    return index >= 0 && index < kBlessingOrderBlessingCount ? words[index] : "";
}

// "might" -> "blessing of might"; "" for an unknown word.
inline std::string BlessingOrderSpell(std::string const& word)
{
    for (int i = 0; i < kBlessingOrderBlessingCount; ++i)
        if (word == BlessingOrderWord(i))
            return "blessing of " + word;
    return std::string();
}

// Per-bot saved value key for one class.
inline std::string BlessingOrderKey(int classIndex)
{
    return std::string("blessing order ") + BlessingOrderClassWord(classIndex);
}

// The stored word is "auto"/"" (automatic) or one of the blessing words.
inline bool IsBlessingOrderWord(std::string const& word)
{
    return word == "auto" || !BlessingOrderSpell(word).empty();
}

// Per-bot saved value key for per-character blessing exceptions.
inline std::string BlessingOrderPlayersKey()
{
    return "blessing order players";
}

inline std::string BlessingOrderLower(std::string value)
{
    for (char& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

inline bool BlessingOrderNamesEqual(std::string const& a, std::string const& b)
{
    return !a.empty() && BlessingOrderLower(a) == BlessingOrderLower(b);
}

struct BlessingPlayerOrder
{
    std::string name;
    std::string word;
};

// Parses `Name:blessing+Name:blessing`, keeping valid entries and the cap.
inline std::vector<BlessingPlayerOrder> ParseBlessingPlayerOrders(std::string const& stored)
{
    std::vector<BlessingPlayerOrder> orders;
    std::size_t start = 0;
    while (start <= stored.size())
    {
        std::size_t const next = stored.find('+', start);
        std::size_t const end = next == std::string::npos ? stored.size() : next;
        if (end > start)
        {
            std::string const token = stored.substr(start, end - start);
            std::size_t const colon = token.find(':');
            if (colon != std::string::npos && colon > 0 && colon + 1 < token.size())
            {
                std::string const name = token.substr(0, colon);
                std::string const word = BlessingOrderLower(token.substr(colon + 1));
                if (!BlessingOrderSpell(word).empty())
                {
                    for (auto it = orders.begin(); it != orders.end(); ++it)
                    {
                        if (BlessingOrderNamesEqual(it->name, name))
                        {
                            orders.erase(it);
                            break;
                        }
                    }
                    orders.push_back({ name, word });
                }
            }
        }
        if (next == std::string::npos)
            break;
        start = next + 1;
    }
    if (orders.size() > kBlessingOrderPlayerCap)
        orders.erase(orders.begin(), orders.begin() + (orders.size() - kBlessingOrderPlayerCap));
    return orders;
}

inline std::string FormatBlessingPlayerOrders(std::vector<BlessingPlayerOrder> const& orders)
{
    std::string out;
    for (BlessingPlayerOrder const& entry : orders)
    {
        if (entry.name.empty() || BlessingOrderSpell(entry.word).empty())
            continue;
        if (!out.empty())
            out += '+';
        out += entry.name + ':' + entry.word;
    }
    return out;
}

// Updates or removes ("auto") one character exception, dropping the oldest
// entry if adding beyond kBlessingOrderPlayerCap.
inline std::string SetBlessingPlayerOrder(
    std::string const& stored, std::string const& playerName, std::string const& word)
{
    std::vector<BlessingPlayerOrder> orders = ParseBlessingPlayerOrders(stored);
    if (playerName.empty())
        return FormatBlessingPlayerOrders(orders);
    for (auto it = orders.begin(); it != orders.end(); ++it)
    {
        if (BlessingOrderNamesEqual(it->name, playerName))
        {
            orders.erase(it);
            break;
        }
    }
    std::string const lowerWord = BlessingOrderLower(word);
    if (!BlessingOrderSpell(lowerWord).empty())
    {
        while (orders.size() >= kBlessingOrderPlayerCap)
            orders.erase(orders.begin());
        orders.push_back({ playerName, lowerWord });
    }
    return FormatBlessingPlayerOrders(orders);
}

inline std::string FindBlessingPlayerOrder(std::string const& stored, std::string const& playerName)
{
    if (playerName.empty())
        return std::string();
    std::vector<BlessingPlayerOrder> const orders = ParseBlessingPlayerOrders(stored);
    for (BlessingPlayerOrder const& entry : orders)
        if (BlessingOrderNamesEqual(entry.name, playerName))
            return entry.word;
    return std::string();
}

// Precedence: per-character exception > class order > auto ("").
inline std::string ResolveBlessingOrderSpell(
    std::string const& playersStored, std::string const& playerName, std::string const& classStored)
{
    std::string const playerWord = FindBlessingPlayerOrder(playersStored, playerName);
    if (!playerWord.empty())
        return BlessingOrderSpell(playerWord);
    return BlessingOrderSpell(classStored);
}
}
