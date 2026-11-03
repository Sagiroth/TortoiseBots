#pragma once

// Pure policy for town errands on order (`.bot downtime`): an owned alt
// following its player temporarily gets the pool's errand kit (class trainer,
// junk vendor, repair) and returns to the master afterwards.
//
// Header-only and free of core/database types on purpose: the rules are
// exercised standalone in tools/test_downtime_policy.cpp. DowntimeService owns
// the live state and feeds these functions plain values.

#include <cstdint>
#include <ctime>
#include <vector>

namespace TortoiseBots
{

// One town errand. Order follows the owner's spec: class trainer first, then
// the junk vendor, then repair. They can share one trip when one NPC serves
// several purposes; the order only decides which one is tried first.
enum class DowntimeErrand : uint8_t
{
    None = 0,
    Trainer,
    Vendor,
    Repair,
};

// A hard window cap so a bot that never arrives (a blocked road, a failed
// route) cannot stay away from its master for good.
constexpr uint32_t kDowntimeMaxSeconds = 15 * 60;

enum class DowntimeStartRefusal : uint8_t
{
    None,
    Dead,
    NotOwnedAlt,
    Autonomous,
};

// Only own alts (`.bot add` companions) get downtime: hired companions are
// temporary mercenaries with their own rules, pool bots belong to the pool,
// and an autonomous alt already runs the pool errands on its own.
inline DowntimeStartRefusal CheckDowntimeStart(bool alive, bool ownedAlt, bool hired, bool randomPool,
    bool autonomous)
{
    if (!alive)
        return DowntimeStartRefusal::Dead;
    if (!ownedAlt || hired || randomPool)
        return DowntimeStartRefusal::NotOwnedAlt;
    if (autonomous)
        return DowntimeStartRefusal::Autonomous;
    return DowntimeStartRefusal::None;
}

// The next errand to run: the first one that is needed and has not been
// attempted in this window (a failed walk must not loop). Returns None when
// every needed errand was tried or nothing is needed.
inline DowntimeErrand NextDowntimeErrand(std::vector<DowntimeErrand> const& tried, bool trainerNeeded,
    bool vendorNeeded, bool repairNeeded)
{
    auto const wasTried = [&tried](DowntimeErrand errand)
    {
        for (DowntimeErrand t : tried)
            if (t == errand)
                return true;
        return false;
    };

    if (trainerNeeded && !wasTried(DowntimeErrand::Trainer))
        return DowntimeErrand::Trainer;
    if (vendorNeeded && !wasTried(DowntimeErrand::Vendor))
        return DowntimeErrand::Vendor;
    if (repairNeeded && !wasTried(DowntimeErrand::Repair))
        return DowntimeErrand::Repair;
    return DowntimeErrand::None;
}

// An errand is finished when its need is gone (the trainer taught what the
// purse covered, the junk is sold, the gear is repaired). The arrival check
// repeats until then, bounded by the window cap above.
inline bool DowntimeErrandDone(DowntimeErrand errand, bool trainerNeeded, bool vendorNeeded, bool repairNeeded)
{
    switch (errand)
    {
        case DowntimeErrand::Trainer:  return !trainerNeeded;
        case DowntimeErrand::Vendor:   return !vendorNeeded;
        case DowntimeErrand::Repair:   return !repairNeeded;
        default:                       return true;
    }
}

// Elapsed-window cap with time_t wraparound is not a concern (epochs), so a
// plain comparison holds.
inline bool DowntimeWindowExpired(time_t startedAt, time_t now)
{
    return now - startedAt >= static_cast<time_t>(kDowntimeMaxSeconds);
}

} // namespace TortoiseBots
