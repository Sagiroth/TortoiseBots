#pragma once

// Pure decision rules for the `use_object` boss-tactic kind: one designated
// bot clicks the game object nearest the boss while the row's aura is up or
// its window has run out, and (mode `tank_anchor`) the tank the boss is on
// walks the boss to that object first. No core includes: callers translate
// game state into plain inputs, so the rules stay testable in
// tools/test_use_object_policy.cpp.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace ai
{
// The clicker walks in early so the boss can be dragged over: the lead in
// milliseconds before a row is due. A crystal worn off-path costs about 12 s
// to walk 80 yd to, hence 15000.
inline constexpr std::uint32_t kUseObjectLeadMs = 15000;
// Two attempts on a record are at least this far apart: the 5 s crystal arm
// plus the 8 s trigger life. An 8 s window keeps a failed click's retry
// inside the 45 s Strength cycle (see the Ossirian row).
inline constexpr std::uint32_t kUseObjectAttemptSpacingMs = 8000;
// A clicker claim without an attempt lapses after this long, so another
// eligible bot takes the next cycle over.
inline constexpr std::uint32_t kUseObjectClaimTimeoutMs = 30000;
// The most attempts a single object takes: the first click arms it, one
// retry covers a fizzle, then the object is burned either way.
inline constexpr std::uint32_t kUseObjectAttemptsPerObject = 2;

// Due = the row's aura is on the boss, or `windowSec` seconds have passed
// since the row's last click (or since the boss was first seen, before any
// click). All times are WorldTimer::getMSTime() differences in unsigned
// arithmetic, so a counter wrap still reads as a small elapsed time.
inline bool UseObjectDue(bool rowHasAura, bool bossHasAura, float windowSec, bool everClicked,
                         std::uint32_t sinceClickMs, std::uint32_t sinceFirstSeenMs)
{
    if (rowHasAura && bossHasAura)
        return true;
    if (windowSec <= 0.0f)
        return false;
    const std::uint32_t sinceMs = everClicked ? sinceClickMs : sinceFirstSeenMs;
    return static_cast<double>(sinceMs) >= static_cast<double>(windowSec) * 1000.0;
}

// Milliseconds until the row is due: 0 when due now, UINT32_MAX when only an
// aura trigger exists and the aura is absent.
inline std::uint32_t UseObjectMsToDue(bool rowHasAura, bool bossHasAura, float windowSec, bool everClicked,
                                      std::uint32_t sinceClickMs, std::uint32_t sinceFirstSeenMs)
{
    if (UseObjectDue(rowHasAura, bossHasAura, windowSec, everClicked, sinceClickMs, sinceFirstSeenMs))
        return 0;
    if (rowHasAura && !bossHasAura)
        return std::numeric_limits<std::uint32_t>::max();
    const std::uint32_t sinceMs = everClicked ? sinceClickMs : sinceFirstSeenMs;
    const double leftMs = static_cast<double>(windowSec) * 1000.0 - static_cast<double>(sinceMs);
    return leftMs <= 0.0 ? 0 : static_cast<std::uint32_t>(leftMs);
}

// One bot that may take the click: the entry is the roster guid, `distanceYd` is
// the 2D distance to the object, `eligible` folds the game-side gates (in
// group, alive, same map, BossTacticsWanted, role matches `priority`, can
// move, passes the `bot_aura` gate, not on explicit stay).
struct UseObjectCandidate
{
    std::uint64_t guid = 0;
    float distanceYd = 0.0f;
    bool eligible = true;
};

// The clicker: the eligible candidate nearest the object, lowest guid on a
// tie. -1 when none is eligible. Every bot computes the same pick from the
// same roster, so all agree without a vote.
inline int PickUseObjectClicker(const std::vector<UseObjectCandidate>& candidates)
{
    int pick = -1;
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        if (!candidates[i].eligible)
            continue;
        if (pick < 0 || candidates[i].distanceYd < candidates[pick].distanceYd ||
            (candidates[i].distanceYd == candidates[pick].distanceYd && candidates[i].guid < candidates[pick].guid))
            pick = static_cast<int>(i);
    }
    return pick;
}

// Whether the click may go out: the row is due, the boss stands within
// `maxYd` (3D, centre to centre) of the object and still (the weakness lands
// 5 s later), and the attempt window is open. A NaN range never passes.
inline bool UseObjectClickReady(bool due, float bossToObject, float maxYd, bool bossMoving, bool attemptReady)
{
    if (!due || !attemptReady || bossMoving)
        return false;
    if (!(bossToObject >= 0.0f) || !(bossToObject <= maxYd))
        return false;
    return true;
}

// The attempt window of one record (shared, instance-wide): at most two
// attempts per object guid, at least 8000 ms between any two attempts, and
// only the claimed clicker may take one. A successful Take frees the claim:
// the next cycle picks again.
struct UseObjectAttempts
{
    std::uint64_t lastObject = 0;
    std::uint32_t lastAttemptMs = 0;
    bool attempted = false;
    std::uint64_t firstObject = 0;
    std::uint32_t firstCount = 0;
    std::uint64_t secondObject = 0;
    std::uint32_t secondCount = 0;

    // The attempts taken on one object so far (the object scan drops an object at kUseObjectAttemptsPerObject).
    std::uint32_t AttemptsFor(std::uint64_t objectGuid) const
    {
        if (objectGuid == firstObject)
            return firstCount;
        if (objectGuid == secondObject)
            return secondCount;
        return 0;
    }

    bool Ready(std::uint64_t objectGuid, std::uint32_t now) const
    {
        if (attempted && now - lastAttemptMs < kUseObjectAttemptSpacingMs)
            return false;
        if (objectGuid == firstObject && firstCount >= kUseObjectAttemptsPerObject)
            return false;
        if (objectGuid == secondObject && secondCount >= kUseObjectAttemptsPerObject)
            return false;
        return true;
    }

    bool Take(std::uint64_t objectGuid, std::uint64_t caller, std::uint64_t clicker, std::uint32_t now)
    {
        if (caller == 0 || caller != clicker)
            return false;
        if (!Ready(objectGuid, now))
            return false;
        if (objectGuid == firstObject)
            ++firstCount;
        else if (objectGuid == secondObject)
            ++secondCount;
        else if (firstObject == 0)
        {
            firstObject = objectGuid;
            firstCount = 1;
        }
        else if (secondObject == 0)
        {
            secondObject = objectGuid;
            secondCount = 1;
        }
        else
            return false;
        lastObject = objectGuid;
        lastAttemptMs = now;
        attempted = true;
        return true;
    }
};

// Whether the tank the boss is on walks it over: the row is nearly due and
// the boss still stands too far from the object to click.
inline bool TankAnchorWanted(bool victimIsThisTank, std::uint32_t msToDue, float bossToObject, float maxYd)
{
    if (!victimIsThisTank || msToDue > kUseObjectLeadMs)
        return false;
    return bossToObject > maxYd * 0.5f;
}

// Where the tank stops: `standoff` (0.4 * maxYd) from the object on the line
// towards the tank, so the boss following into melee ends inside click range.
// Degenerate input (the tank on the object): the object itself.
inline void TankAnchorPoint(float objX, float objY, float tankX, float tankY, float standoff, float& x, float& y)
{
    const float dx = tankX - objX;
    const float dy = tankY - objY;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (!(dist > 0.0f))
    {
        x = objX;
        y = objY;
        return;
    }
    x = objX + dx / dist * standoff;
    y = objY + dy / dist * standoff;
}
}  // namespace ai
