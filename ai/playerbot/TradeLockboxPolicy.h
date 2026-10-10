#pragma once

namespace ai
{
    // Trade lockbox unlock gate (tradelock, AG-5/AG-9).
    //
    // A rogue bot mid-trade should pick a locked box parked in the trader's
    // do-not-trade slot instead of letting the trade complete around it.
    // Donor mod-playerbots parses the extended-trade packet for a locked
    // NONTRADED slot; here the unlock action already reads the box from
    // TradeData, so the packet only needs to wake it - and the wake must
    // stay silent for everyone without a pickable box (trade updates arrive
    // on every window change, so an ungated action would chat on every
    // tick). The fine checks (skill, spell, level) stay in Execute, which
    // still tells when it runs.
    //
    // Pure decision rule, no core includes: the caller passes the rogue
    // flag plus the already-read box state.

    // Usefulness verdict: rogue, a box present, locked, not yet unlocked.
    inline bool TradeLockboxUseful(bool isRogue, bool hasBox, bool boxLocked,
        bool boxUnlocked)
    {
        return isRogue && hasBox && boxLocked && !boxUnlocked;
    }
}
