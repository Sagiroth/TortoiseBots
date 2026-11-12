#pragma once
#include <cstdint>
#include <string>

// Pure words and keys for `.bot blessing`: per paladin, the blessing each
// class gets ("auto" or unset keeps the automatic coverage pick). No core
// includes, so tools/test_blessing_order_policy.cpp can run it.

namespace ai
{
constexpr int kBlessingOrderClassCount = 9;
constexpr int kBlessingOrderBlessingCount = 6;

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
}
