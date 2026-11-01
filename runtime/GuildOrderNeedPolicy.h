#pragma once

// Need-list recovery for guild orders: a row parked in Waiting/needitems
// re-runs the ladder when its facts change, so a rule deployed later (or a
// restocked bank, a new listing, a fuller purse) resumes the order instead
// of waiting on the player forever. Header-only and free of core/database
// types on purpose: exercised standalone in tools/test_guild_order_need_policy.cpp.
// The store feeds this header plain readiness flags it already computed;
// the header only picks the leg and gates the cadence.
#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// A parked need row re-checks the ladder this often: one cheap bank look per
// need item plus in-memory vendor/AH facts, never per bot tick. Matches the
// keep-watch cadence, so need rows and stocked rows cost alike. The
// ten-second bags/bank wake below stays the fast lane; this is the backstop
// for facts it cannot see (a newly vendor-unlimited item, a new AH listing,
// a bank the denial flag had excluded).
inline constexpr uint32_t kNeedLadderCheckMs = 10 * 60 * 1000;

inline bool NeedLadderCheckDue(uint32_t nowMs, uint32_t lastCheckMs)
{
    return nowMs - lastCheckMs >= kNeedLadderCheckMs;
}

// Every third slow check with nothing ready re-runs the whole ladder: a
// farm or sub-order source may have appeared since the park (a fix deployed,
// a skill gained, a new guild member), and the store cannot plan farms. A
// ladder that still finds nothing parks the same list again, which does not
// whisper twice.
inline constexpr uint32_t kNeedFullRetryEveryChecks = 3;

inline bool NeedFullRetryDue(uint32_t ladderChecks)
{
    return ladderChecks > 0 && ladderChecks % kNeedFullRetryEveryChecks == 0;
}

// Where a parked need item resumes once its facts changed: the vendor leg
// buys a vendor-unlimited item, the bank leg fetches a restocked bank, the
// AH leg re-tries a newly listed item.
enum class NeedResumeLeg : uint8_t { None, Vendor, Bank, AuctionHouse, Ladder };

inline NeedResumeLeg NeedResumeLegFor(bool vendorReady, bool bankReady, bool ahReady, bool retryLadder = false)
{
    if (vendorReady)
        return NeedResumeLeg::Vendor;
    if (bankReady)
        return NeedResumeLeg::Bank;
    if (ahReady)
        return NeedResumeLeg::AuctionHouse;
    if (retryLadder)
        return NeedResumeLeg::Ladder;
    return NeedResumeLeg::None;
}

} // namespace GuildOrders
} // namespace TortoiseBots
