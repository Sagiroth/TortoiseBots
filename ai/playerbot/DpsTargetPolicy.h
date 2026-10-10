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
// Donor constants (cast-time physics, not taste) — used by the buckets below.
inline float DpsOverkillLifetime() { return 5.0f; }
inline float DpsPreferredLifetimeMax() { return 30.0f; }

namespace ai
{
    // Caster buckets (donor CasterFindTargetSmartStrategy::GetIntervalLevel):
    // [5-30 s] in range (12) > low/out-of-window in range (11) >
    // [5-30 s] out of range (2) > rest out of range (1/0). In-range adds 10.
    // Sub-5 s mobs rank second, never skipped (review PR #581): when every
    // attacker is nearly dead the tournament still returns one. Higher wins.
    inline int CasterTargetBucket(float lifetime, bool inRange)
    {
        int level = inRange ? 10 : 0;
        if (lifetime >= DpsOverkillLifetime() && lifetime <= DpsPreferredLifetimeMax())
            return level + 2;
        if (lifetime > DpsPreferredLifetimeMax())
            return level;
        return level + 1;
    }

    // General/combo buckets (donor GeneralFindTargetSmartStrategy): in range
    // (10) beats out of range (0); lifetime only orders within a bucket.
    inline int GeneralTargetBucket(bool inRange) { return inRange ? 10 : 0; }
}
