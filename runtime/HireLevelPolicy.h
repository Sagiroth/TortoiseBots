#pragma once

// Issue #936: hire a companion at a chosen level. Pure, core-free policy for
// `.bot hire <class> ... [level]`: the level-word parser and the clamp rule.
// Unit-tested standalone (tools/test_hire_level_policy.cpp). The command, the
// service and the tests all read this header, so the rule cannot drift.
//
// Rule: a hire's level runs from 1 up to the hirer's level plus
// AiPlayerbot.HireMaxLevelAboveHirer (default 0: never above the hirer),
// never above 60. A GM hirer is not limited by the hirer-relative cap, only
// by the level range 1..60. Below the hirer has no limit except 1. No level
// requested keeps today's behaviour: the hire arrives at the hirer's level.

#include <cstddef>
#include <cstdint>
#include <string>

namespace TortoiseBots
{

constexpr uint32_t kHireLevelMin = 1;
constexpr uint32_t kHireLevelMax = 60;
// A bare trailing number is only read as a level from here up: lower numbers
// are race, gender and class ids in the same command ("warrior 5" is an
// undead warrior today). Use level=<n> for lower levels.
constexpr uint32_t kHireBareLevelMin = 12;

enum class HireLevelWord
{
    NotLevel,  // the token is not a level word; the caller parses it as before
    Ok,        // levelOut holds the requested level (1..999, not yet clamped)
    Bad        // a level word without a usable number: refuse
};

namespace detail
{
// Digits only, 1..3 of them, no sign. levelOut gets the value (0 allowed so
// the caller can report it as out of range).
inline bool ParseSmallNumber(std::string const& s, uint32_t& out)
{
    if (s.empty() || s.size() > 3)
        return false;
    uint32_t v = 0;
    for (char c : s)
    {
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + static_cast<uint32_t>(c - '0');
    }
    out = v;
    return true;
}
} // namespace detail

// `level=<n>`, `level:<n>`, `lvl=<n>`, `lvl:<n>`; word is lower case (the
// command lowercases its tokens). A recognised prefix with a missing,
// non-numeric, zero or longer-than-three-digit number is Bad so the command
// refuses instead of silently hiring at the hirer's level. A number above 60
// is Ok here and is clamped by ResolveHireLevel.
inline HireLevelWord ParseHireLevelWord(std::string const& word, uint32_t& levelOut)
{
    std::size_t prefix = 0;
    if (word.compare(0, 5, "level") == 0)
        prefix = 5;
    else if (word.compare(0, 3, "lvl") == 0)
        prefix = 3;
    else
        return HireLevelWord::NotLevel;
    if (word.size() <= prefix || (word[prefix] != '=' && word[prefix] != ':'))
        return HireLevelWord::NotLevel;
    uint32_t value = 0;
    if (!detail::ParseSmallNumber(word.substr(prefix + 1), value) || value < kHireLevelMin)
        return HireLevelWord::Bad;
    levelOut = value;
    return HireLevelWord::Ok;
}

// A bare number as the LAST token of the command. Only numbers that no
// race/gender/class id can be (>= kHireBareLevelMin) count, so every command
// that parsed before still parses the same.
inline bool ParseBareHireLevel(std::string const& word, uint32_t& levelOut)
{
    uint32_t value = 0;
    if (!detail::ParseSmallNumber(word, value) || value < kHireBareLevelMin)
        return false;
    levelOut = value;
    return true;
}

// Highest level this hirer may request. aboveHirer is the config key; a GM
// ignores it and may go to 60.
inline uint32_t HireMaxAllowedLevel(uint32_t hirerLevel, uint32_t aboveHirer, bool isGm)
{
    if (isGm)
        return kHireLevelMax;
    uint32_t cap = hirerLevel + aboveHirer;
    if (cap > kHireLevelMax)
        cap = kHireLevelMax;
    if (cap < kHireLevelMin)
        cap = kHireLevelMin;
    return cap;
}

struct HireLevelResult
{
    uint32_t level = kHireLevelMin;  // the level to hire at
    bool clamped = false;            // a request above the cap was lowered
    uint32_t maxAllowed = kHireLevelMax;
};

// requested 0 = none asked: the hirer's level (clamped to 1..60), as before.
// Otherwise requested is lowered to the cap when above it; below the hirer is
// free down to 1.
inline HireLevelResult ResolveHireLevel(uint32_t hirerLevel, uint32_t requested,
    uint32_t aboveHirer, bool isGm)
{
    HireLevelResult r;
    r.maxAllowed = HireMaxAllowedLevel(hirerLevel, aboveHirer, isGm);
    if (requested == 0)
    {
        uint32_t own = hirerLevel < kHireLevelMin ? kHireLevelMin : hirerLevel;
        r.level = own > kHireLevelMax ? kHireLevelMax : own;
        return r;
    }
    if (requested > r.maxAllowed)
    {
        r.level = r.maxAllowed;
        r.clamped = true;
        return r;
    }
    r.level = requested < kHireLevelMin ? kHireLevelMin : requested;
    return r;
}

} // namespace TortoiseBots
