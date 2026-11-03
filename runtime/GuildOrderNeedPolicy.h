#pragma once

// Need-list recovery for guild orders: an item only the player can bring sits
// on the row's need list while the bot keeps sourcing every other component,
// so the list is valid on a working row too, not only on a row parked in
// Waiting/needitems. A parked row re-runs the ladder when its facts change,
// so a rule deployed later (or a restocked bank, a new listing, a fuller
// purse) resumes the order instead of waiting on the player forever.
// Header-only and free of core/database types on purpose: exercised
// standalone in tools/test_guild_order_need_policy.cpp. The store feeds this
// header plain readiness flags it already computed; the header only picks the
// leg and gates the cadence.
#include "GuildOrderContract.h"

#include <cstddef>
#include <cstdint>
#include <vector>

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

inline bool NeedListHas(std::vector<NeedItem> const& need, uint32_t itemId)
{
    if (!itemId)
        return false;
    for (NeedItem const& item : need)
        if (item.itemId == itemId && item.count)
            return true;
    return false;
}

// The failed sub-order's item joins the list with the larger count; a new id
// goes to the back, so the order of first appearance survives.
inline void MergeNeedItem(std::vector<NeedItem>& need, uint32_t itemId, uint32_t count)
{
    if (!itemId || !count)
        return;
    for (NeedItem& item : need)
    {
        if (item.itemId != itemId)
            continue;
        if (count > item.count)
            item.count = count;
        return;
    }
    need.push_back(NeedItem{ itemId, count });
}

// The persisted token back to entries ("id:count", ';'-separated, written by
// NeedItemsToken); anything unparsable is dropped.
inline std::vector<NeedItem> ParseNeedItems(std::string const& token)
{
    std::vector<NeedItem> out;
    size_t start = 0;
    while (start < token.size())
    {
        size_t end = token.find(';', start);
        std::string entry = token.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        size_t colon = entry.find(':');
        if (colon != std::string::npos)
        {
            try
            {
                unsigned long itemId = std::stoul(entry.substr(0, colon));
                unsigned long count = std::stoul(entry.substr(colon + 1));
                if (itemId && count && itemId <= UINT32_MAX && count <= UINT32_MAX)
                    out.push_back(NeedItem{ static_cast<uint32_t>(itemId), static_cast<uint32_t>(count) });
            }
            catch (...)
            {
            }
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return out;
}

// Same entries, same order, same counts: the compare-and-set the prune's
// write-back needs.
inline bool NeedItemsEqual(std::vector<NeedItem> const& a, std::vector<NeedItem> const& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].itemId != b[i].itemId || a[i].count != b[i].count)
            return false;
    return true;
}

// The player hears about a list once per fact: an item id the last whisper
// never named, or one whose count grew above what it said. A rebuild that
// only repeats or shrinks the whispered list (a lane hit, a full retry, a
// sibling sub-order failure, an item delivered) stays silent.
inline bool NeedListNeedsWhisper(std::string const& lastToken, std::vector<NeedItem> const& now)
{
    std::vector<NeedItem> whispered = ParseNeedItems(lastToken);
    for (NeedItem const& item : now)
    {
        if (!item.itemId || !item.count)
            continue;
        uint32_t toldCount = 0;
        for (NeedItem const& was : whispered)
            if (was.itemId == item.itemId && was.count > toldCount)
                toldCount = was.count;
        if (item.count > toldCount)
            return true;
    }
    return false;
}

// What the player has been told so far: the last token's entries kept, every
// entry of `now` merged in at the larger count. A list rebuilt one item at a
// time grows this the same way the sends did, so the next rebuild never
// re-whispers.
inline std::string NeedWhisperTokenUnion(std::string const& lastToken, std::vector<NeedItem> const& now)
{
    std::vector<NeedItem> spoken = ParseNeedItems(lastToken);
    for (NeedItem const& item : now)
        MergeNeedItem(spoken, item.itemId, item.count);
    return NeedItemsToken(spoken);
}

// A dead-end reason belongs to the leg that produced it, not to the row: a
// working row that takes an item on its need list turns to another item, and
// the stale verdict must not follow it. NoSource would drop the next
// candidate's vendor and farm legs, BankDenied or NoBanker its bank leg.
// NoListing is the auction leg's verdict in the same way: a stale
// empty-counter mark must not keep the next item off the auction house.
inline OrderReason ResetDeadEndReasonOnWorking(OrderReason reason)
{
    if (reason == OrderReason::NoSource || reason == OrderReason::BankDenied ||
        reason == OrderReason::NoBanker || reason == OrderReason::NoAuctioneer ||
        reason == OrderReason::NoListing)
        return OrderReason::None;
    return reason;
}

// A lane hit or a failed sibling clears the need list; the current supply leg
// is unrelated work and survives, unless it sources one of the items now on
// the list (that leg would just re-request a dead end).
inline bool KeepLegOnNeedClear(uint32_t supplyItemId, std::vector<NeedItem> const& needItems)
{
    return supplyItemId != 0 && !NeedListHas(needItems, supplyItemId);
}

// The whole-tree walk's count is the live shortage (the player brought part of
// it, or the bot got it some other way): an entry the walk no longer names
// leaves the list, an entry still named keeps the list's order but the walk's
// count.
inline void PruneNeedItems(std::vector<NeedItem>& need, std::vector<NeedItem> const& missingNow)
{
    size_t kept = 0;
    for (NeedItem const& item : need)
    {
        uint32_t count = 0;
        for (NeedItem const& missing : missingNow)
            if (missing.itemId == item.itemId && missing.count > count)
                count = missing.count;
        if (!count)
            continue;
        need[kept] = NeedItem{ item.itemId, count };
        ++kept;
    }
    need.resize(kept);
}

// What a craft order does with what is left: supplyable work first, then a
// running sub-order (work, not the player's items), then a park, and a plan
// with nothing left to wait for goes back to the ladder.
enum class PartialNeedVerdict : uint8_t { Supply, WaitOnSubOrders, Park };

inline PartialNeedVerdict PartialNeedVerdictFor(bool supplyableLeft, bool subOrdersRunning, bool needListed)
{
    if (supplyableLeft)
        return PartialNeedVerdict::Supply;
    if (subOrdersRunning)
        return PartialNeedVerdict::WaitOnSubOrders;
    if (needListed)
        return PartialNeedVerdict::Park;
    return PartialNeedVerdict::Supply;
}

// A parent is waiting on work, not idle, while it has no supply leg of its
// own and a child row is still unfinished: the ladder returned
// WaitOnSubOrders (step None), or the sub step stays on while the placed
// child works. Then the no-progress clock, the farm give-up and the zone
// move all stay paused. A parent working its own leg (bank, farm, auction,
// craft) keeps every clock, so a stalled leg of its own still ends the round
// and hands over to the ladder; a request that never became a running row
// stops counting as a child after its staleness window.
inline bool PauseOrderClocks(SupplyStep step, int unfinishedChildren)
{
    return (step == SupplyStep::None || step == SupplyStep::Sub) && unfinishedChildren > 0;
}

// The player hears about a list once, as soon as it exists, on a working row
// as well as on a parked one.
inline bool NeedWhisperDue(OrderState state, OrderReason reason, bool needListed, bool whispered)
{
    if (!needListed || whispered)
        return false;
    return state == OrderState::Working || (state == OrderState::Waiting && reason == OrderReason::NeedItems);
}

// The bank and ladder lanes serve parked need rows and working rows that carry
// a need list; the bank lane also keeps its slow look at a Sub leg.
inline bool NeedBankLaneEligible(OrderState state, OrderReason reason, SupplyStep step, bool needListed)
{
    if (state == OrderState::Waiting && reason == OrderReason::NeedItems)
        return true;
    return state == OrderState::Working && (step == SupplyStep::Sub || needListed);
}

inline bool NeedLadderLaneEligible(OrderState state, OrderReason reason, bool needListed)
{
    if (state == OrderState::Waiting && reason == OrderReason::NeedItems)
        return true;
    return state == OrderState::Working && needListed;
}

// One recipe material, as the ladder's walk saw it.
enum class MaterialOutcome : uint8_t { Owned, Missing, Craftable, Blocked };

struct MaterialFold
{
    MaterialOutcome outcome = MaterialOutcome::Owned;
    size_t index = 0; // the picked material; the material count when none was picked
};

// A missing reagent comes first (raw materials before intermediates), then a
// craftable component, then a blocked one (on the need list itself, or nested
// under a blocked component); all owned (or no material at all) means the item
// itself can be crafted now.
inline MaterialFold FoldMaterials(MaterialOutcome const* outcomes, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (outcomes[i] == MaterialOutcome::Missing)
            return MaterialFold{ MaterialOutcome::Missing, i };
    for (size_t i = 0; i < count; ++i)
        if (outcomes[i] == MaterialOutcome::Craftable)
            return MaterialFold{ MaterialOutcome::Craftable, i };
    for (size_t i = 0; i < count; ++i)
        if (outcomes[i] == MaterialOutcome::Blocked)
            return MaterialFold{ MaterialOutcome::Blocked, count };
    return MaterialFold{ MaterialOutcome::Owned, count };
}

} // namespace GuildOrders
} // namespace TortoiseBots
