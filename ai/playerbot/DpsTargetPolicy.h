#pragma once

// DpsTargetPolicy — pure DPS target-tournament rules (LD-1/LD-4,
// mod-playerbots parity).
//
// Donor behaviour (mod-playerbots, read-only reference,
// `src/Ai/Base/Value/DpsTargetValue.cpp`): DPS pick via estimated-lifetime
// buckets instead of flat least-HP. Casters avoid starting casts on mobs
// that die before the cast lands (<5 s of group DPS); combo classes stick to
// their combo target; everyone skips the CC-moon mark (icon 4) and snaps to
// the skull mark (icon 7) mid-tournament with a sticky flag.
//
// The three bucket shapes below are pure data (health, dps, distance,
// ranges) so they are decided here and tested on their own; the Unit*
// tournament that consumes them lives in DpsTargetValue.cpp. Thresholds are
// donor constants (5 s overkill line, 5-30 s preferred window), not config:
// they describe cast-time physics, not taste.

// Preferred lifetime window (seconds) for a fresh cast to be worthwhile.
inline float DpsOverkillLifetime() { return 5.0f; }
inline float DpsPreferredLifetimeMax() { return 30.0f; }

namespace ai
{
    // Caster buckets (donor CasterFindTargetSmartStrategy::GetIntervalLevel):
    // [5-30 s] in range (12) > low/out-of-window in range (11) >
    // [5-30 s] out of range (2) > rest out of range (1/0). In-range adds 10.
    // Returns the bucket; higher wins.
    inline int CasterTargetBucket(float lifetime, bool inRange)
    {
        int level = inRange ? 10 : 0;
        if (lifetime >= 5.0f && lifetime <= 30.0f)
            return level + 2;
        if (lifetime > 30.0f)
            return level;
        return level + 1;
    }

    // General/combo buckets (donor GeneralFindTargetSmartStrategy): in range
    // (10) beats out of range (0); lifetime only orders within a bucket.
    inline int GeneralTargetBucket(bool inRange) { return inRange ? 10 : 0; }

    // Whether the caster tournament even considers this attacker: CC-moon is
    // always skipped (handled by caller via icon check); a mob with less
    // than 5 s of life left is overkill — starting a cast on it wastes it.
    inline bool CasterSkipsOverkill(float lifetime) { return lifetime < 5.0f; }
}
