#pragma once

// `.bot gear`: gear one bot or a bound raid to a hire tier under an
// item-level cap. Pure policy: grammar, preset expansion, cap filter, bag
// purge decision and the TBM:GEAR summary line. The service
// (runtime/BotGearService) owns the world-thread pass.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace BotGear
{

constexpr char const* kPresetsDefault = "prebis=t1 ilvl=63;lkh=t2 ilvl=63;zg=t2 ilvl=63;aq20=t2 ilvl=63;t1=t3 ilvl=66;"
    "timbermaw=t3 ilvl=66;onyxia=t3 ilvl=66;mc=t3 ilvl=66;t2=t4 ilvl=76;bwl=t4 ilvl=76;es=t4 ilvl=76;"
    "aq40=t5 ilvl=78;naxx=t6 ilvl=83;kara40=t7 ilvl=88";
constexpr uint32_t kGearBotsPerTick = 2;
constexpr uint8_t kMaxTier = 7;

// 1.12 item class and inventory type numbers, kept here so this header stays
// core-free (the service passes the prototype values straight through).
constexpr uint32_t ITEM_CLASS_CONSUMABLE = 0;
constexpr uint32_t ITEM_CLASS_CONTAINER = 1;
constexpr uint32_t ITEM_CLASS_WEAPON = 2;
constexpr uint32_t ITEM_CLASS_ARMOR = 4;
constexpr uint32_t INVTYPE_NON_EQUIP = 0;
constexpr uint32_t INVTYPE_CHEST = 5;
constexpr uint32_t INVTYPE_BAG = 18;
constexpr uint32_t INVTYPE_AMMO = 24;
constexpr uint32_t INVTYPE_QUIVER = 27;

enum class Target : uint8_t { Invalid, Bot, Raid };

struct Args
{
    Target target = Target::Invalid;
    std::string name;
    uint8_t tier = 0;
    uint32_t ilvlCap = 0;
    bool enchants = true;
    std::string preset;
    std::string error;
};

struct BotResult
{
    std::string name;
    std::string role;
    bool geared = false;
    std::string skip;
    float avgIlvl = 0;
    uint32_t cappedSlots = 0;
    uint32_t purged = 0;
};

inline std::string LowerWord(std::string const& word)
{
    std::string out = word;
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

inline std::vector<std::string> SplitWords(std::string const& text)
{
    std::vector<std::string> words;
    std::string cur;
    for (char c : text)
    {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            if (!cur.empty())
            {
                words.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur += c;
    }
    if (!cur.empty())
        words.push_back(cur);
    return words;
}

inline bool ParseTierNumber(std::string const& word, uint8_t& tier)
{
    if (word.empty() || word.size() > 3)
        return false;
    for (char c : word)
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    unsigned long value = std::stoul(word);
    if (value > kMaxTier)
        return false;
    tier = static_cast<uint8_t>(value);
    return true;
}

inline bool ParseCapNumber(std::string const& word, uint32_t& cap)
{
    if (word.empty() || word.size() > 3)
        return false;
    for (char c : word)
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    unsigned long value = std::stoul(word);
    if (value < 1 || value > 200)
        return false;
    cap = static_cast<uint32_t>(value);
    return true;
}

inline bool IsKeyword(std::string const& lower)
{
    if (lower == "tier" || lower == "preset" || lower == "noenchants")
        return true;
    if (lower.size() > 1 && lower[0] == 't')
    {
        for (size_t i = 1; i < lower.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(lower[i])))
                return false;
        return true;
    }
    return lower.size() > 5 && lower.compare(0, 5, "ilvl=") == 0;
}

struct Preset
{
    std::string name;
    uint8_t tier = 0;
    uint32_t ilvlCap = 0;
};

inline std::vector<Preset> ParsePresets(std::string const& presets)
{
    std::vector<Preset> out;
    size_t start = 0;
    while (start <= presets.size())
    {
        size_t end = presets.find(';', start);
        std::string entry = presets.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t eq = entry.find('=');
        if (eq != std::string::npos)
        {
            Preset p;
            p.name = LowerWord(entry.substr(0, eq));
            for (std::string const& word : SplitWords(entry.substr(eq + 1)))
            {
                std::string lower = LowerWord(word);
                if (lower.size() > 1 && lower[0] == 't' && lower.find('=') == std::string::npos)
                    ParseTierNumber(lower.substr(1), p.tier);
                else if (lower.size() > 5 && lower.compare(0, 5, "ilvl=") == 0)
                    ParseCapNumber(lower.substr(5), p.ilvlCap);
            }
            if (!p.name.empty())
                out.push_back(p);
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return out;
}

inline Args Parse(std::string const& text, std::string const& presets)
{
    Args args;
    std::vector<std::string> words = SplitWords(text);
    if (words.empty())
    {
        args.error = "usage";
        return args;
    }
    size_t i = 0;
    if (LowerWord(words[0]) == "raid")
    {
        if (words.size() < 2 || IsKeyword(LowerWord(words[1])))
        {
            args.error = "usage";
            return args;
        }
        args.target = Target::Raid;
        args.name = words[1];
        i = 2;
    }
    else
    {
        args.target = Target::Bot;
        args.name = words[0];
        i = 1;
    }
    std::vector<Preset> table = ParsePresets(presets);
    bool sawTier = false;
    for (; i < words.size(); ++i)
    {
        std::string lower = LowerWord(words[i]);
        if (lower == "tier")
        {
            if (++i >= words.size() || !ParseTierNumber(words[i], args.tier))
            {
                args.error = "tier";
                return args;
            }
            sawTier = true;
        }
        else if (lower.size() > 1 && lower[0] == 't' && lower.find('=') == std::string::npos &&
            (lower.size() == 2 || lower.size() == 3))
        {
            bool allDigits = true;
            for (size_t k = 1; k < lower.size(); ++k)
                allDigits = allDigits && std::isdigit(static_cast<unsigned char>(lower[k]));
            if (!allDigits || !ParseTierNumber(lower.substr(1), args.tier))
            {
                args.error = "tier";
                return args;
            }
            sawTier = true;
        }
        else if (lower.size() > 5 && lower.compare(0, 5, "ilvl=") == 0)
        {
            if (!ParseCapNumber(lower.substr(5), args.ilvlCap))
            {
                args.error = "ilvl";
                return args;
            }
        }
        else if (lower == "noenchants")
        {
            args.enchants = false;
        }
        else if (lower == "preset")
        {
            if (++i >= words.size())
            {
                args.error = "preset";
                return args;
            }
            std::string want = LowerWord(words[i]);
            bool found = false;
            for (Preset const& p : table)
            {
                if (p.name != want)
                    continue;
                args.preset = p.name;
                args.tier = p.tier;
                args.ilvlCap = p.ilvlCap;
                sawTier = sawTier || p.tier > 0;
                found = true;
                break;
            }
            if (!found)
            {
                args.error = "preset";
                return args;
            }
        }
        else
        {
            args.error = "usage";
            return args;
        }
    }
    if (!sawTier && args.ilvlCap == 0)
    {
        args.error = args.tier == 0 ? "nothing" : "usage";
        return args;
    }
    if (args.tier == 0 && args.ilvlCap == 0)
    {
        args.error = "nothing";
        return args;
    }
    return args;
}

inline bool RowAllowed(uint32_t itemLevel, uint32_t cap)
{
    return cap == 0 || itemLevel <= cap;
}

inline bool PurgeBagItem(uint32_t itemClass, uint32_t inventoryType, uint32_t itemLevel, uint32_t cap, uint32_t wornMax)
{
    if (itemClass != ITEM_CLASS_WEAPON && itemClass != ITEM_CLASS_ARMOR)
        return false;
    if (inventoryType == INVTYPE_NON_EQUIP || inventoryType == INVTYPE_AMMO ||
        inventoryType == INVTYPE_BAG || inventoryType == INVTYPE_QUIVER)
        return false;
    uint32_t limit = cap != 0 ? cap : wornMax;
    return itemLevel > limit;
}

inline std::string SummaryLine(uint8_t tier, uint32_t cap, std::vector<BotResult> const& results)
{
    uint32_t geared = 0, capped = 0, purged = 0;
    double tankSum = 0, healerSum = 0, dpsSum = 0;
    uint32_t tankN = 0, healerN = 0, dpsN = 0;
    std::string skipped;
    for (BotResult const& r : results)
    {
        if (r.geared)
        {
            ++geared;
            if (r.role == "tank")
            {
                tankSum += r.avgIlvl;
                ++tankN;
            }
            else if (r.role == "healer")
            {
                healerSum += r.avgIlvl;
                ++healerN;
            }
            else
            {
                dpsSum += r.avgIlvl;
                ++dpsN;
            }
        }
        else if (!r.skip.empty())
        {
            if (!skipped.empty())
                skipped += ",";
            skipped += r.name + "(" + r.skip + ")";
        }
        capped += r.cappedSlots;
        purged += r.purged;
    }
    char tankAvg[32], healerAvg[32], dpsAvg[32];
    if (tankN > 0)
        std::snprintf(tankAvg, sizeof(tankAvg), "%.1f", tankSum / tankN);
    else
        std::snprintf(tankAvg, sizeof(tankAvg), "-");
    if (healerN > 0)
        std::snprintf(healerAvg, sizeof(healerAvg), "%.1f", healerSum / healerN);
    else
        std::snprintf(healerAvg, sizeof(healerAvg), "-");
    if (dpsN > 0)
        std::snprintf(dpsAvg, sizeof(dpsAvg), "%.1f", dpsSum / dpsN);
    else
        std::snprintf(dpsAvg, sizeof(dpsAvg), "-");
    char line[1024];
    std::snprintf(line, sizeof(line), "TBM:GEAR|%s|%u|%u|%u|%u|%s|%s|%s|%u|%s|%u",
        geared == results.size() ? "ok" : (geared == 0 ? "none" : "partial"),
        geared, static_cast<uint32_t>(results.size()), tier, cap,
        tankAvg, healerAvg, dpsAvg, capped, skipped.c_str(), purged);
    return line;
}

} // namespace BotGear
} // namespace TortoiseBots
