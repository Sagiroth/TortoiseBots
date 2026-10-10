#pragma once

#include <cstdint>

namespace ai
{
    // Battleground forced objective-reset gate (bgforcereset, SOC-P6).
    //
    // The force action full-stops the bot and re-picks its objective. It
    // fires from the dead engine (DeadStrategy `dead` node) and the per-map
    // `timer bg` watchdog (Warsong/Alterac, ~60 s). Without a gate it would
    // run every tick while corpse-running (churning the role roll and the
    // path) and could stop the bot mid-fight. Donor mod-playerbots has no
    // gate (its `dead` node fires every tick while dead); this port adds
    // one: inside the match, out of combat, at most once a minute.
    //
    // Pure decision rule, no core includes: the caller (BGTactics, which
    // already holds the flag-carrier check) passes the already-known BG /
    // combat state plus the latched "bg force reset at" manual-time anchor.

    // Minimum seconds between two forced resets (matches the ~60 s `timer
    // bg` cadence, so the watchdog normally re-arms just as the latch
    // releases; the dead path fires once per death-ish).
    inline std::int64_t BgForceResetCooldownSec() { return 60; }

    // Manual-time anchor key (per-bot value, zero cost when unset).
    inline char const* BgForceResetAnchorKey() { return "bg force reset at"; }

    // Usefulness verdict: outside a match or mid-fight never (a mid-fight
    // stop feeds the enemy a standing target); otherwise at most once per
    // cooldown so corpse-run ticks and timer ticks cannot churn.
    inline bool BgForceResetUseful(bool inBattleGround, bool inCombat,
        std::int64_t anchorTime, std::int64_t now,
        std::int64_t cooldownSec = BgForceResetCooldownSec())
    {
        if (!inBattleGround || inCombat)
            return false;
        if (anchorTime == 0)
            return true;
        return now - anchorTime >= cooldownSec;
    }
}
