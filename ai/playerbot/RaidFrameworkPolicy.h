#pragma once

#include <cstdint>
#include <string>

// Pure decision rules for the raid-tactics framework (mod-playerbots
// parity batch 1). No core includes: callers translate game state into
// these plain inputs, so the rules stay testable in
// tools/test_raid_framework_policy.cpp without the server.

namespace ai
{
// Neglect-threat is read-once (Get resets to false), so fight multipliers
// must Set it on every evaluation while their boss is engaged. A single
// Set at strategy-add time leaks exactly one threat-bypass read, then the
// flag goes quiet while the fight still needs it.
inline bool NeglectThreatNeedsRefresh(bool setThisEvaluation)
{
    return setThisEvaluation;
}

// "find target" qualifier matching: case-insensitive substring, so
// "loatheb" and "anub'rekhan" hit "Loatheb" and "Anub'Rekhan". Empty
// qualifier never matches (donor returns null on empty qualifier).
inline bool RaidNameMatches(const std::string& unitName, const std::string& qualifier)
{
    if (qualifier.empty() || unitName.empty())
        return false;
    std::string unit = unitName;
    std::string want = qualifier;
    for (char& c : unit)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : want)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return unit.find(want) != std::string::npos;
}

// Map IDs for the classic-raid auto-enable rows. 509 = Ruins of Ahn'Qiraj
// (AQ20), 309 = Zul'Gurub, 531 = Ahn'Qiraj Temple (AQ40), 533 = Naxxramas.
inline bool IsClassicRaidMap(std::uint32_t mapId)
{
    return mapId == 509 || mapId == 309 || mapId == 531 || mapId == 533;
}
} // namespace ai
