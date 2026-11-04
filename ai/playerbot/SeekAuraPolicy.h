#pragma once

// The seek_aura boss tactic's pure rules (Zul'Gurub's Hakkar and his Poisonous Blood 24321): when the raid gathers at
// the row's spot B, who walks into the source's cloud, when the source may be damaged, and when the off-tank pulls
// another Son of Hakkar 11357. Tested in tools/test_seek_aura_policy.cpp; callers pass plain numbers so this header
// stays free of core includes, and the row's columns arrive exactly as BossTacticsMgr reads them.

#include <cstdint>

namespace ai
{
    constexpr float kSeekRunYdPerS = 7.0f;           // run speed
    constexpr std::uint32_t kSeekLatencyMs = 500;    // decision and start of the walk
    constexpr float kSeekCloudInsetYd = 1.5f;        // how far inside the cloud edge a bot aims
    constexpr std::uint32_t kSeekCloseMs = 3000;     // the window closes this long before due
    constexpr std::uint32_t kSeekForceMs = 15000;    // the burn is forced this long before due
    constexpr std::uint32_t kSeekGatherMaxMs = 8000; // a gather never waits longer than this
    constexpr float kSeekSpotYd = 8.0f;              // the source is "at B" within this
    constexpr float kSeekPickupYd = 60.0f;           // a source this near B may be pulled
    constexpr int kSeekPullMinMissing = 3;           // pull a source only while this many seekers lack the aura
    constexpr float kSeekGatherStartPct = 5.0f;      // seekers start walking at hold % + this

    // Cycle clock: the first sighting of the boss in combat with a victim starts it, and every accepted sighting of the
    // row's cycle spell on a group member restarts it. lastCycleMs 0 means no sighting yet.
    inline std::uint32_t SeekDueMs(std::uint32_t engageMs, std::uint32_t lastCycleMs, std::uint32_t cycleMs)
    {
        return (lastCycleMs ? lastCycleMs : engageMs) + cycleMs;
    }

    // A sighting starts a new cycle only when no cycle is known or it is at least half a cycle after the last one: all
    // the raid's bots see the same siphon within a few hundred milliseconds, so the second sighting must not move the
    // due time again.
    inline bool SeekCycleAccepts(std::uint32_t lastCycleMs, std::uint32_t seenMs, std::uint32_t cycleMs)
    {
        if (lastCycleMs == 0)
            return true;
        return seenMs >= lastCycleMs + cycleMs / 2;
    }

    // The dose window [due - lead, due - kSeekCloseMs], both ends inclusive.
    inline bool SeekWindowOpen(std::uint32_t nowMs, std::uint32_t dueMs, std::uint32_t leadMs)
    {
        return nowMs + leadMs >= dueMs && nowMs + kSeekCloseMs <= dueMs;
    }

    // The burn is forced once the window is about to close.
    inline bool SeekForceBurn(std::uint32_t nowMs, std::uint32_t dueMs)
    {
        return nowMs + kSeekForceMs >= dueMs;
    }

    // Who seeks: every living bot that lacks the aura, except the boss's victim (the main tank) and the two tanks kept
    // off the poison (the cause-insanity backup), and never a charmed bot.
    inline bool SeekWanted(bool keyOn, bool windowOpen, bool hasAura, bool isBossVictim, bool isExemptTank, bool charmed,
        bool alive)
    {
        return keyOn && windowOpen && alive && !hasAura && !isBossVictim && !isExemptTank && !charmed;
    }

    // A cloud is the row's when its spell is the aura itself or the trigger spell of its effect.
    inline bool IsSoughtCloud(std::uint32_t dynSpellId, std::uint32_t effectTriggerSpell, std::uint32_t soughtAura)
    {
        return dynSpellId == soughtAura || effectTriggerSpell == soughtAura;
    }

    // The bot gets into the cloud before it expires: it has to run the distance from its position to the aim point
    // (radius - inset) inside the cloud, within the cloud's remaining life minus the decision latency.
    inline bool SeekCloudReachable(float distToCentre, float radius, std::uint32_t remainingMs)
    {
        float const run = distToCentre - (radius - kSeekCloudInsetYd);
        if (run <= 0.0f)
            return true;
        if (remainingMs <= kSeekLatencyMs)
            return false;
        float const budget = kSeekRunYdPerS * (static_cast<float>(remainingMs - kSeekLatencyMs) / 1000.0f);
        return run <= budget;
    }

    // The gather is ready when nobody is missing, when it has waited kSeekGatherMaxMs, or when the burn is forced (the
    // window is about to close, so the source dies whichever way).
    inline bool SeekGatherReady(int missing, std::uint32_t gatherStartMs, std::uint32_t nowMs, bool forceBurn)
    {
        return missing <= 0 || forceBurn || nowMs - gatherStartMs >= kSeekGatherMaxMs;
    }

    // DPS may not damage a source outside the dose window at all; inside it they stop at the hold percent until the
    // gather is ready, and the burn after readiness is not held again.
    inline bool SeekSourceHeld(bool windowOpen, float hpPct, float holdPct, bool gatherReady)
    {
        if (!windowOpen)
            return true;
        return !gatherReady && hpPct <= holdPct;
    }

    // The second off-tank pulls a new source while the window is open, no source is in the fight yet, enough seekers
    // still miss the aura, and a source stands near the spot.
    inline bool SeekTankPullDue(bool windowOpen, bool sourceInFight, int missing, float nearestSourceToSpotYd)
    {
        return windowOpen && !sourceInFight && missing >= kSeekPullMinMissing &&
            nearestSourceToSpotYd <= kSeekPickupYd;
    }
}
