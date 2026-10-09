#pragma once

#include <cstdint>
#include <ctime>

namespace ai
{
    // Ready-check rebuff defer (readyrebuff).
    //
    // Without it a bot answers a ready check the instant it arrives and then
    // buffs through the pull: the raid sees ready, the pull starts, and the
    // bot is still casting buffs. Donor mod-playerbots defers the confirm
    // while a force-rebuff window runs (ReadyCheckAction + ForceRebuffState +
    // ReadyReplyAction) and replies once buffs settle.
    //
    // This port keeps the donor shape but a lighter state: no rebuff
    // strategy, no buff-cycle hooks, no heal suppression. When the config
    // gate is on and a real ready-check packet arrives out of combat, the
    // action reports readiness text immediately (so the master sees status),
    // stamps an anchor, and holds the confirm packet. A pending trigger then
    // fires a reply action once the bot is no longer mid-cast and a short
    // grace has passed (buffs flow through the normal per-tick engine in the
    // meantime), with a hard cap that always replies so a chained cast
    // sequence can never wedge the check.
    //
    // Pure decision rule, no core includes: the caller passes the anchor it
    // keeps in a "manual time" value, the current time, and whether the bot
    // is mid-cast. The reply itself stays in the action.

    // Grace before an unhurried reply: buff casts get a chance to land.
    inline std::int64_t ReadyRebuffGraceSec() { return 8; }
    // Hard cap: always reply by now, even mid-cast. Ready checks last tens
    // of seconds; holding longer risks the pull leaving without an answer.
    inline std::int64_t ReadyRebuffCapSec() { return 30; }

    // Manual-time anchor key (per-bot value, zero cost when unset).
    inline char const* ReadyRebuffAnchorKey() { return "rebuff ready since"; }

    // Reply verdict: no anchor means nothing deferred. Inside the grace
    // window hold (let the buff pass run). Past grace, reply once not
    // mid-cast; past the cap, reply regardless so the check can never wedge.
    inline bool ReadyRebuffDue(std::int64_t anchorTime, std::int64_t now,
        bool isCasting, std::int64_t graceSec = ReadyRebuffGraceSec(),
        std::int64_t capSec = ReadyRebuffCapSec())
    {
        if (anchorTime == 0)
            return false;
        std::int64_t age = now - anchorTime;
        if (age >= capSec)
            return true;
        if (age < graceSec)
            return false;
        return !isCasting;
    }
}
