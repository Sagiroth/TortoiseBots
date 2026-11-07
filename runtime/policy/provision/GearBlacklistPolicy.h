#pragma once

// Owner-configured gear exclusion list (`AiPlayerbot.RandomGearBlacklist`):
// a blocked item id is never handed out by any auto-gear path. The blacklist
// wins over the whitelist, and a pool or hired bot already wearing a blocked
// item treats the slot as empty on the next gear pass; a real player's and an
// owned bot's own gear is never touched.
//
// On top of the list, auto-gear never hands out items players cannot really
// have: no known source (GM, developer and unreleased items), retired or
// test data, item level 0 outside the cosmetic shirt/tabard slots, and
// legendary or better. A whitelisted id skips these rules, the blacklist
// does not.
//
// Pure policy: no core/engine headers, unit-tested standalone
// (tools/test_gear_blacklist_policy.cpp).

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>

namespace TortoiseBots
{

inline bool GearBlacklistBlocks(std::list<uint32_t> const& blacklist, uint32_t itemId)
{
    for (uint32_t blocked : blacklist)
        if (blocked == itemId)
            return true;
    return false;
}

// The whitelist seeds an item regardless of the scoring rules, unless the
// blacklist holds the same id.
inline bool GearWhitelistApplies(std::list<uint32_t> const& blacklist, bool whitelisted, uint32_t itemId)
{
    if (!whitelisted)
        return false;
    return !GearBlacklistBlocks(blacklist, itemId);
}

// Test, placeholder and retired item names in any letter case. "test" counts
// only as a whole word, so "Testament of Hope" and "The Greatest Wizzard Hat"
// pass while "Martin Thunder3 jesse test" does not.
inline bool GearNameLooksRetired(std::string const& name)
{
    std::string lower(name);
    for (char& ch : lower)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    for (char const* marker : { "deprecated", "unused", "placeholder", "[ph]", "(old)", "zzz" })
        if (lower.find(marker) != std::string::npos)
            return true;

    auto isLetter = [](char ch) { return std::isalpha(static_cast<unsigned char>(ch)) != 0; };
    for (size_t at = lower.find("test"); at != std::string::npos; at = lower.find("test", at + 1))
    {
        bool const startsWord = at == 0 || !isLetter(lower[at - 1]);
        bool const endsWord = at + 4 >= lower.size() || !isLetter(lower[at + 4]);
        if (startsWord && endsWord)
            return true;
    }
    return false;
}

inline constexpr uint32_t kGearQualityLegendary = 5;

struct BotGearFacts
{
    bool blacklisted = false;
    bool whitelisted = false;
    bool knownSource = true;   // loot, vendor, quest, craft or container
    bool retired = false;      // deprecated flag or a test/retired name
    bool cosmeticSlot = false; // shirt or tabard
    uint32_t itemLevel = 1;
    uint32_t quality = 0;
};

inline bool BotGearForbidden(BotGearFacts const& facts)
{
    if (facts.blacklisted)
        return true;
    if (facts.whitelisted)
        return false;
    if (!facts.knownSource || facts.retired)
        return true;
    if (facts.itemLevel == 0 && !facts.cosmeticSlot)
        return true;
    return facts.quality >= kGearQualityLegendary;
}

// A pool or hired bot already wearing a forbidden item re-gears the slot: the
// worn piece counts as empty, so it is replaced, or removed when nothing
// passes. A real player and an owned bot keep whatever they wear.
inline bool GearWornCountsAsEmpty(bool keepsWornGear, bool wornForbidden)
{
    if (keepsWornGear)
        return false;
    return wornForbidden;
}

} // namespace TortoiseBots
