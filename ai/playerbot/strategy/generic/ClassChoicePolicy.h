#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Pure words and keys for `.bot choice`: one ordered aura/aspect/curse,
// poison per hand, sting or demon per bot that has the kind. No core
// includes, so tools/test_choice_policy.cpp can run it. Model mirrors `.bot blessing`/`.bot totem`: an order is one stored
// "manual saved string" value, "auto"/unset keeps the automatic pick.

namespace ai
{
// Kinds in wire order; the command refuses anything else. Kind words use
// only letters and '_' (the addon matches them with [%a_]+).
enum ChoiceKind
{
    CHOICE_AURA = 0,
    CHOICE_ASPECT = 1,
    CHOICE_CURSE = 2,
    CHOICE_POISON_MH = 3,
    CHOICE_POISON_OH = 4,
    CHOICE_STING = 5,
    CHOICE_PET = 6,
    CHOICE_KIND_COUNT = 7,
};

inline char const* ChoiceKindWord(int index)
{
    switch (index)
    {
    case CHOICE_AURA: return "aura";
    case CHOICE_ASPECT: return "aspect";
    case CHOICE_CURSE: return "curse";
    case CHOICE_POISON_MH: return "poison_mh";
    case CHOICE_POISON_OH: return "poison_oh";
    case CHOICE_STING: return "sting";
    case CHOICE_PET: return "pet";
    default: return "";
    }
}

inline int ChoiceKindIndex(std::string const& word)
{
    for (int i = 0; i < CHOICE_KIND_COUNT; ++i)
        if (!word.empty() && word == ChoiceKindWord(i))
            return i;
    return -1;
}

// The class that carries the kind (CLASS_* ids): aura paladin, aspect and
// sting hunter, curse and demon warlock, poisons rogue.
inline std::uint8_t ChoiceKindClassId(int index)
{
    switch (index)
    {
    case CHOICE_AURA: return 2;
    case CHOICE_ASPECT: return 3;
    case CHOICE_CURSE: return 9;
    case CHOICE_POISON_MH: return 4;
    case CHOICE_POISON_OH: return 4;
    case CHOICE_STING: return 3;
    case CHOICE_PET: return 9;
    default: return 0;
    }
}

// Every kind the class has, in wire order (a hunter has aspect and sting).
inline std::vector<int> ChoiceKindsForClass(std::uint8_t classId)
{
    std::vector<int> kinds;
    for (int i = 0; i < CHOICE_KIND_COUNT; ++i)
        if (ChoiceKindClassId(i) == classId)
            kinds.push_back(i);
    return kinds;
}

// Poisons are bag items, not spells: "known" means a usable poison item.
inline bool ChoiceKindUsesItem(int kind)
{
    return kind == CHOICE_POISON_MH || kind == CHOICE_POISON_OH;
}

// The short word the player types ("retribution"), without the spell noise.
inline char const* ChoiceWord(int kind, int index)
{
    static char const* const auras[] = { "devotion", "retribution", "concentration",
        "sanctity", "shadow", "fire", "frost" };
    static char const* const aspects[] = { "hawk", "monkey", "cheetah", "pack", "wild", "beast", "wolf", "viper" };
    static char const* const curses[] = { "agony", "elements", "shadow", "recklessness",
        "weakness", "tongues", "doom", "exhaustion" };
    static char const* const poisons[] = { "instant", "deadly", "crippling", "mind", "wound" };
    static char const* const stings[] = { "serpent", "scorpid", "viper" };
    static char const* const pets[] = { "imp", "voidwalker", "succubus", "felhunter" };
    switch (kind)
    {
    case CHOICE_AURA: return index >= 0 && index < 7 ? auras[index] : "";
    case CHOICE_ASPECT: return index >= 0 && index < 8 ? aspects[index] : "";
    case CHOICE_CURSE: return index >= 0 && index < 8 ? curses[index] : "";
    case CHOICE_POISON_MH:
    case CHOICE_POISON_OH: return index >= 0 && index < 5 ? poisons[index] : "";
    case CHOICE_STING: return index >= 0 && index < 3 ? stings[index] : "";
    case CHOICE_PET: return index >= 0 && index < 4 ? pets[index] : "";
    default: return "";
    }
}

inline int ChoiceWordCount(int kind)
{
    switch (kind)
    {
    case CHOICE_AURA: return 7;
    case CHOICE_ASPECT: return 8;
    case CHOICE_CURSE: return 8;
    case CHOICE_POISON_MH:
    case CHOICE_POISON_OH: return 5;
    case CHOICE_STING: return 3;
    case CHOICE_PET: return 4;
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
// A poison names its item family ("mind-numbing poison"), not a spell.
inline std::string ChoiceSpell(int kind, std::string const& word)
{
    if (ChoiceWordIndex(kind, word) < 0)
        return std::string();
    switch (kind)
    {
    case CHOICE_AURA:
        if (word == "shadow" || word == "fire" || word == "frost")
            return word + " resistance aura";
        return word + " aura";
    case CHOICE_ASPECT:
        return "aspect of the " + word;
    case CHOICE_CURSE:
        if (word == "elements")
            return "curse of the elements";
        return "curse of " + word;
    case CHOICE_POISON_MH:
    case CHOICE_POISON_OH:
        return (word == "mind" ? std::string("mind-numbing") : word) + " poison";
    case CHOICE_STING:
        return word + " sting";
    case CHOICE_PET:
        return "summon " + word;
    default:
        return std::string();
    }
}

// Poison item entries (every rank) per poison word.
inline std::vector<std::uint32_t> ChoicePoisonItemIds(std::string const& word)
{
    if (word == "instant")
        return { 6947, 6949, 6950, 8926, 8927, 8928, 21927 };
    if (word == "deadly")
        return { 2892, 2893, 8984, 8985, 20844, 22053, 22054 };
    if (word == "crippling")
        return { 3775, 3776 };
    if (word == "mind")
        return { 5237, 6951, 9186 };
    if (word == "wound")
        return { 10918, 10920, 10921, 10922, 22055 };
    return {};
}

// The weapon temporary-enchant ids each poison word leaves on the weapon.
inline std::vector<std::uint32_t> ChoicePoisonEnchantIds(std::string const& word)
{
    if (word == "instant")
        return { 323, 324, 325, 623, 624, 625, 2641, 3768, 3769 };
    if (word == "deadly")
        return { 7, 8, 626, 627, 2630, 2642, 2643, 3770, 3771 };
    if (word == "crippling")
        return { 22, 603 };
    if (word == "mind")
        return { 35, 23, 643 };
    if (word == "wound")
        return { 703, 704, 705, 706, 2644, 3772, 3773 };
    return {};
}

// The stored word with "auto" read as no order.
inline std::string ChoiceOrderedWord(int kind, std::string const& stored)
{
    if (stored.empty() || stored == "auto" || ChoiceWordIndex(kind, stored) < 0)
        return std::string();
    return stored;
}

// While a poison (per hand) or sting order is set, only the row for the
// ordered word may run; every automatic row of that kind stands down, so the
// automatic kit and the order never overwrite each other.
inline bool ChoiceRowAllowed(std::string const& orderedWord, std::string const& rowWord)
{
    return orderedWord.empty() || orderedWord == rowWord;
}

// Ordered demon: swap only out of combat; any demon but the Imp needs a Soul
// Shard unless the bot has the item cheat.
enum class ChoicePetDecision
{
    LeaveAlone,
    Summon,
    WaitCombat,
    NoShard,
};

inline ChoicePetDecision DecideChoicePet(std::string const& orderedWord, bool knowsSummon, bool currentIsOrdered,
    bool inCombat, bool hasShard, bool itemCheat)
{
    if (orderedWord.empty() || !knowsSummon || currentIsOrdered)
        return ChoicePetDecision::LeaveAlone;
    if (inCombat)
        return ChoicePetDecision::WaitCombat;
    if (orderedWord != "imp" && !hasShard && !itemCheat)
        return ChoicePetDecision::NoShard;
    return ChoicePetDecision::Summon;
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

// `no curse` only claims the target slot when no `.bot choice` curse is active.
inline bool ShouldNoCurseTriggerFire(std::string const& orderedCurse)
{
    return orderedCurse.empty();
}
}
