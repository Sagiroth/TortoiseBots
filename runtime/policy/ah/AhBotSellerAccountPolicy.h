#pragma once

// Pure policy for the AH bot seller account and its per-faction characters.
// Std-only headers (no core includes) so standalone unit tests compile with
// plain g++.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ai
{

inline constexpr size_t kAhBotMaxAccountNameLen = 16;
inline constexpr uint32_t kAhBotAllianceHouseId = 1;
inline constexpr uint32_t kAhBotHordeHouseId = 6;
inline constexpr uint32_t kAhBotNeutralHouseId = 7;

enum class SellerFaction : uint8_t
{
    Alliance = 0,
    Horde = 1,
};

// Vanilla + Turtle WoW 1.18.1 playable race IDs (SharedDefines.h).
inline constexpr std::array<uint8_t, 5> kSellerAllianceRaces = { 1, 3, 4, 7, 10 }; // Human, Dwarf, NightElf, Gnome, HighElf
inline constexpr std::array<uint8_t, 5> kSellerHordeRaces = { 2, 5, 6, 8, 9 };     // Orc, Undead, Tauren, Troll, Goblin

inline bool IsAllianceRace(uint8_t race)
{
    for (uint8_t r : kSellerAllianceRaces)
        if (r == race)
            return true;
    return false;
}

inline bool IsHordeRace(uint8_t race)
{
    for (uint8_t r : kSellerHordeRaces)
        if (r == race)
            return true;
    return false;
}

inline bool FactionForSellerRace(uint8_t race, SellerFaction& out)
{
    if (IsAllianceRace(race))
    {
        out = SellerFaction::Alliance;
        return true;
    }
    if (IsHordeRace(race))
    {
        out = SellerFaction::Horde;
        return true;
    }
    return false;
}

inline uint8_t PickSellerRace(SellerFaction faction, uint32_t roll)
{
    if (faction == SellerFaction::Alliance)
        return kSellerAllianceRaces[roll % kSellerAllianceRaces.size()];
    return kSellerHordeRaces[roll % kSellerHordeRaces.size()];
}

inline char const* SellerFactionName(SellerFaction faction)
{
    return faction == SellerFaction::Alliance ? "alliance" : "horde";
}

inline bool ParseSellerFaction(std::string const& text, SellerFaction& out)
{
    std::string lower;
    lower.reserve(text.size());
    for (unsigned char c : text)
        lower.push_back(static_cast<char>(std::tolower(c)));
    if (lower == "alliance")
    {
        out = SellerFaction::Alliance;
        return true;
    }
    if (lower == "horde")
    {
        out = SellerFaction::Horde;
        return true;
    }
    return false;
}

inline std::string TrimAccountName(std::string const& raw)
{
    size_t first = raw.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return std::string();
    size_t last = raw.find_last_not_of(" \t\r\n");
    return raw.substr(first, last - first + 1);
}

inline std::string NormalizeAccountNameUpper(std::string const& raw)
{
    std::string trimmed = TrimAccountName(raw);
    for (char& c : trimmed)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return trimmed;
}

inline std::string NormalizeAccountNameLower(std::string const& raw)
{
    std::string trimmed = TrimAccountName(raw);
    for (char& c : trimmed)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return trimmed;
}

inline bool SellerAccountNameEquals(std::string const& a, std::string const& b)
{
    return NormalizeAccountNameLower(a) == NormalizeAccountNameLower(b);
}

inline bool SellerAccountHasPrefix(std::string const& accountName, std::string const& prefix)
{
    std::string trimmedPrefix = TrimAccountName(prefix);
    if (trimmedPrefix.empty())
        return false;
    std::string lowerName = NormalizeAccountNameLower(accountName);
    std::string lowerPrefix = NormalizeAccountNameLower(trimmedPrefix);
    return lowerName.size() >= lowerPrefix.size() &&
        lowerName.compare(0, lowerPrefix.size(), lowerPrefix) == 0;
}

// Account name must be 1..16 printable non-space ASCII characters and must not
// start with the random-bot account prefix (case-insensitive) so pool tools
// can never mistake the AH bot account for a disposable pool account.
inline bool IsValidSellerAccountName(std::string const& accountName, std::string const& randomBotPrefix)
{
    std::string trimmed = TrimAccountName(accountName);
    if (trimmed.empty() || trimmed.size() > kAhBotMaxAccountNameLen)
        return false;
    for (unsigned char c : trimmed)
    {
        if (c <= 0x20 || c >= 0x7f)
            return false;
    }
    std::string effectivePrefix = TrimAccountName(randomBotPrefix);
    if (effectivePrefix.empty())
        effectivePrefix = "RNDBOT";
    return !SellerAccountHasPrefix(trimmed, effectivePrefix);
}

// WoW player-like character name: 2..12 ASCII letters, capitalized first
// letter and lowercase remainder (matches BotNames.h NormalizeBotName).
inline bool IsValidSellerCharacterName(std::string const& name)
{
    if (name.size() < 2 || name.size() > 12)
        return false;
    if (name[0] < 'A' || name[0] > 'Z')
        return false;
    for (size_t i = 1; i < name.size(); ++i)
    {
        if (name[i] < 'a' || name[i] > 'z')
            return false;
    }
    return true;
}

inline bool IsReadyForSellers(bool accountExists, size_t allianceCount, size_t hordeCount)
{
    return accountExists && allianceCount >= 1 && hordeCount >= 1;
}

inline uint32_t PickSellerGuidForHouse(uint32_t auctionHouseId,
    std::vector<uint32_t> const& allianceGuids,
    std::vector<uint32_t> const& hordeGuids,
    uint32_t roll)
{
    if (auctionHouseId == kAhBotAllianceHouseId)
        return allianceGuids.empty() ? 0 : allianceGuids[roll % allianceGuids.size()];
    if (auctionHouseId == kAhBotHordeHouseId)
        return hordeGuids.empty() ? 0 : hordeGuids[roll % hordeGuids.size()];
    if (auctionHouseId == kAhBotNeutralHouseId)
    {
        size_t total = allianceGuids.size() + hordeGuids.size();
        if (total == 0)
            return 0;
        size_t idx = roll % total;
        return idx < allianceGuids.size() ? allianceGuids[idx] : hordeGuids[idx - allianceGuids.size()];
    }
    return 0;
}

struct SellerRepairPlan
{
    bool needCreateAccount = false;
    bool needRotatePassword = false;
    bool needCleanupStaleRows = false;
    uint32_t createAllianceCount = 0;
    uint32_t createHordeCount = 0;
    bool healthy = false;
};

inline SellerRepairPlan DecideSellerRepair(bool accountNameValid,
    bool accountExists,
    bool passwordRotatedThisRun,
    uint32_t targetPerFaction,
    uint32_t validAllianceCount,
    uint32_t validHordeCount,
    bool hasStaleRows)
{
    SellerRepairPlan plan;
    if (!accountNameValid || targetPerFaction == 0)
        return plan;

    if (!accountExists)
    {
        plan.needCreateAccount = true;
        plan.needCleanupStaleRows = hasStaleRows;
        plan.createAllianceCount = targetPerFaction;
        plan.createHordeCount = targetPerFaction;
        return plan;
    }

    plan.needRotatePassword = !passwordRotatedThisRun;
    plan.needCleanupStaleRows = hasStaleRows;
    if (validAllianceCount < targetPerFaction)
        plan.createAllianceCount = targetPerFaction - validAllianceCount;
    if (validHordeCount < targetPerFaction)
        plan.createHordeCount = targetPerFaction - validHordeCount;

    plan.healthy = !plan.needCleanupStaleRows &&
        plan.createAllianceCount == 0 &&
        plan.createHordeCount == 0;
    return plan;
}

inline bool IsSellerRepairDue(bool initialCheckDone, uint32_t elapsedMs, uint32_t intervalSeconds)
{
    if (!initialCheckDone)
        return true;
    uint64_t intervalMs = static_cast<uint64_t>(std::max<uint32_t>(1, intervalSeconds)) * 1000ULL;
    return static_cast<uint64_t>(elapsedMs) >= intervalMs;
}

} // namespace ai
