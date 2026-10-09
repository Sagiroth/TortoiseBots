#pragma once

#include <cstdint>
#include <string>

// Pure decision rules for the raid-tactics framework (mod-playerbots
// parity: ThreatValues.h "neglect threat", TargetValue.h "find target").
// No core includes: testable in tools/test_raid_framework_policy.cpp.
// This header is the single home of name matching — RaidTargetValues.cpp
// calls ai::RaidNameMatches rather than carrying its own copy.

namespace ai
{
// "find target" qualifier matching: case-insensitive FULL-name equality
// like the donor (TargetValue.cpp:179 requires equal lengths). Substring
// matching is wrong here: "emperor vek" would match both twins and
// "c'thun" the Eye and C'Thun, handing tactics a nondeterministic boss
// pointer. Callers pass full lowercase names ("ossirian the unscarred").
// Empty qualifier never matches (donor returns null on empty).
inline bool RaidNameMatches(const std::string& unitName, const std::string& qualifier)
{
    if (qualifier.empty() || unitName.empty())
        return false;
    if (unitName.size() != qualifier.size())
        return false;
    for (std::size_t i = 0; i < unitName.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(unitName[i])) !=
            std::tolower(static_cast<unsigned char>(qualifier[i])))
            return false;
    }
    return true;
}

// Map IDs for the classic-raid auto-enable rows. 509 = Ruins of Ahn'Qiraj
// (AQ20), 309 = Zul'Gurub, 531 = Ahn'Qiraj Temple (AQ40), 533 = Naxxramas.
inline bool IsClassicRaidMap(std::uint32_t mapId)
{
    return mapId == 509 || mapId == 309 || mapId == 531 || mapId == 533;
}

// Read-once "neglect threat" contract (donor NeglectThreatResetValue):
// Get resets to false, so fight multipliers must Set on EVERY evaluation
// while their boss is engaged — a single Set at strategy-add leaks exactly
// one bypass read, then the flag goes quiet mid-fight. The engine must
// also evaluate the setting multiplier before ThreatMultiplier each tick,
// or the first-evaluated action consumes the flag. Documented here because
// it cannot be unit-tested without the value object; the Get/Reset
// behavior itself is donor-verbatim in RaidTargetValues.h.
inline constexpr bool kNeglectThreatSetPerEvaluation = true;
} // namespace ai
