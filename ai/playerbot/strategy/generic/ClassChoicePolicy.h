#pragma once
#include <cstdint>
#include <string>

// Pure words and keys for `.bot choice`: one ordered aura/aspect/curse per
// bot that has the kind. No core includes, so tools/test_choice_policy.cpp can
// run it. Model mirrors `.bot blessing`/`.bot totem`: an order is one stored
// "manual saved string" value, "auto"/unset keeps the automatic pick.

namespace ai
{
// Kinds in wire order; the command refuses anything else.
inline int ChoiceKindIndex(std::string const& word)
{
    if (word == "aura")
        return 0;
    if (word == "aspect")
        return 1;
    if (word == "curse")
        return 2;
    return -1;
}

inline char const* ChoiceKindWord(int index)
{
    switch (index)
    {
    case 0: return "aura";
    case 1: return "aspect";
    case 2: return "curse";
    default: return "";
    }
}

// The class that carries the kind (CLASS_* ids): aura paladin, aspect
// hunter, curse warlock.
inline std::uint8_t ChoiceKindClassId(int index)
{
    switch (index)
    {
    case 0: return 2;
    case 1: return 3;
    case 2: return 9;
    default: return 0;
    }
}

inline int ChoiceKindForClass(std::uint8_t classId)
{
    switch (classId)
    {
    case 2: return 0;
    case 3: return 1;
    case 9: return 2;
    default: return -1;
    }
}

// The short word the player types ("retribution"), without the spell noise.
inline char const* ChoiceWord(int kind, int index)
{
    static char const* const auras[] = { "devotion", "retribution", "concentration",
        "sanctity", "shadow", "fire", "frost" };
    static char const* const aspects[] = { "hawk", "monkey", "cheetah", "pack", "wild", "beast", "wolf", "viper" };
    static char const* const curses[] = { "agony", "elements", "shadow", "recklessness",
        "weakness", "tongues", "doom", "exhaustion" };
    switch (kind)
    {
    case 0: return index >= 0 && index < 7 ? auras[index] : "";
    case 1: return index >= 0 && index < 8 ? aspects[index] : "";
    case 2: return index >= 0 && index < 8 ? curses[index] : "";
    default: return "";
    }
}

inline int ChoiceWordCount(int kind)
{
    switch (kind)
    {
    case 0: return 7;
    case 1: return 8;
    case 2: return 8;
    default: return 0;
    }
}

inline int ChoiceWordIndex(int kind, std::string const& word)
{
    for (int i = 0; i < ChoiceWordCount(kind); ++i)
        if (word == ChoiceWord(kind, i))
            return i;
    return -1;
}

// Full spell the order casts. "shadow"/"fire"/"frost" auras and the
// "elements" curse expand to their longer names; the rest prefix directly.
inline std::string ChoiceSpell(int kind, std::string const& word)
{
    if (ChoiceWordIndex(kind, word) < 0)
        return std::string();
    if (kind == 0)
    {
        if (word == "shadow" || word == "fire" || word == "frost")
            return word + " resistance aura";
        return word + " aura";
    }
    if (kind == 1)
        return "aspect of the " + word;
    if (word == "elements")
        return "curse of the elements";
    return "curse of " + word;
}

// Per-bot saved value key for one kind.
inline std::string ChoiceOrderKey(int kind)
{
    return std::string("choice ") + ChoiceKindWord(kind);
}

// The stored word is "auto"/"" (automatic) or a choice word for the kind.
inline bool IsChoiceWord(int kind, std::string const& word)
{
    return word == "auto" || ChoiceWordIndex(kind, word) >= 0;
}
}
