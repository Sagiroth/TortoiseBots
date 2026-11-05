#pragma once

// The seek_aura boss tactic's pure rules (Zul'Gurub's Hakkar and his Poisonous Blood 24321): when the raid gathers at
// the row's spot B, who walks into the source's cloud, when the source may be damaged, and when the off-tank pulls
// another Son of Hakkar 11357. Tested in tools/test_seek_aura_policy.cpp; callers pass plain numbers so this header
// stays free of core includes, and the row's columns arrive exactly as BossTacticsMgr reads them.

#include <cstdint>
#include <vector>

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

    // A source already at or below the hold percent and fighting the raid may be finished wherever it stands: held
    // until the next window, it reset and healed back to full.
    inline bool SeekSourceFinishable(bool inCombat, float hpPct, float holdPct)
    {
        return inCombat && hpPct <= holdPct;
    }

    // DPS may not damage a source outside the dose window at all; inside it they stop at the hold percent until the
    // gather is ready, and the burn after readiness is not held again. A finishable source is never held.
    inline bool SeekSourceHeld(bool windowOpen, float hpPct, float holdPct, bool gatherReady, bool finishable = false)
    {
        if (finishable)
            return false;
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

    // The seek action takes this tick while it walks, and once more to stop a walk already under way. Standing at the
    // gather consumes nothing (the stand action's rule): the engine runs one successful action a tick, so a seek that
    // kept returning true at the spot would silence the rotation, the heals and the burn for the rest of the window.
    inline bool SeekActionConsumesTick(bool atSpot, bool moving)
    {
        return !atSpot || moving;
    }

    // A source at the spot, as the gather's pick sees it: its guid and the health % that breaks the tie.
    struct SeekSourceChoice
    {
        std::uint64_t guid = 0;
        float hpPct = 100.0f;
    };

    // The gather's one source: the current one while it still stands at the spot (two Sons there must not make each
    // bot pick its own, which flipped the shared latch), else the lowest health % and then the lowest guid, so every
    // bot of the fight reads the same source. 0: no source at the spot.
    inline std::uint64_t SeekKeepOrPickGatherSource(std::uint64_t currentGuid,
        const std::vector<SeekSourceChoice>& atSpot)
    {
        std::uint64_t best = 0;
        float bestHp = 0.0f;
        for (const SeekSourceChoice& c : atSpot)
        {
            if (currentGuid != 0 && c.guid == currentGuid)
                return currentGuid;
            if (best == 0 || c.hpPct < bestHp || (c.hpPct == bestHp && c.guid < best))
            {
                best = c.guid;
                bestHp = c.hpPct;
            }
        }
        return best;
    }

    // The source search's reach around the spot: the spot's own radius while the third tank is there to drag a source
    // in, else the fallback's 35 yd (the gather then goes to the source wherever it stands). A forced burn or a source
    // already at the hold percent reaches the fallback too, whatever the tank count: the off-spot Son has to die for
    // its cloud, and waiting for a drag at the spot left it alive.
    constexpr float kSeekFallbackYd = 35.0f;

    inline float SeekSourceSearchYd(bool thirdTank, bool forceBurn, bool belowHold)
    {
        if (forceBurn || belowHold)
            return kSeekFallbackYd;
        return thirdTank ? kSeekSpotYd : kSeekFallbackYd;
    }

    // The record is only touched while the bots see the boss fighting, so a longer gap is a re-pull: the cycle clock
    // starts over rather than counting the old engage (due in the past, the window shut until the first siphon).
    constexpr std::uint32_t kSeekRepullGapMs = 10000;

    inline bool SeekClockStartOver(bool bossInCombat, std::uint32_t lastSeenMs, std::uint32_t nowMs)
    {
        return !bossInCombat || (lastSeenMs != 0 && nowMs - lastSeenMs > kSeekRepullGapMs);
    }
}
