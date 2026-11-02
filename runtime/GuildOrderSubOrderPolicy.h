#pragma once

// Sub-order picking rules for guild orders: which guild bot makes or farms an
// item the parent cannot get itself. Header-only and free of core types so
// the rules stay unit tested on their own
// (tools/test_guild_order_sub_order_policy.cpp). The store feeds this header
// plain candidate verdicts it already computed per bot; the header only
// ranks and gates them.
#include <cstddef>
#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

// Sub-orders on by default; workers B (bank) and the ladder share the shape.
inline constexpr uint32_t kSubOrderDefaultDepth = 3;
// Busy-only queued requests retry on this interval, not every world tick.
inline constexpr uint32_t kSubOrderRetryMs = 45000;
// An unfinished roster slice retries on the next tick instead: the planner
// budget ran out (or the carried candidate went stale) while a scan was
// already under way, and waiting the full busy interval would stall the
// branch for no reason.
inline constexpr uint32_t kSubOrderSliceRetryMs = 1000;
// Consecutive busy-only full scans before the request gives up onto the
// parent's need list: bots cycling their own orders must not hold a
// component forever.
inline constexpr uint32_t kSubOrderBusyScanLimit = 10;
// Planner evaluations per PlaceSubOrders tick: each SubOrderCandidateUsable
// call may walk the source index, so a tick with many due requests shares
// the budget instead of stalling the world thread. A request whose scan hit
// the budget keeps the best candidate and the busy verdict it already found
// and retries with the scan resumed after the last member it looked at; the
// sub-order is placed once the walk has covered the whole roster, so the
// pick ranks every member rather than just the current slice.
inline constexpr size_t kSubOrderPlannerEvalsPerTick = 8;

// Whether a queued request is due. A request without a retry time is due at
// once: the signed difference wraps once the world uptime passes 2^31 ms,
// which would park every fresh request for the rest of a long session.
inline bool SubOrderRequestDue(uint32_t nextTryMs, uint32_t nowMs)
{
    if (!nextTryMs)
        return true;
    return int32_t(nextTryMs - nowMs) <= 0;
}

// Drain order for due requests, keys ascending by (parent, item): the first
// request after the last one the drain served leads this time, so a request
// that spent the planner budget on one tick does not lead every tick.
// Returns 0 when nothing follows that key (wrap to the start).
inline size_t SubOrderDrainOffset(uint32_t const* parents, uint32_t const* items,
    size_t count, uint32_t lastParent, uint32_t lastItem)
{
    if (!parents || !items || !count || (!lastParent && !lastItem))
        return 0;
    for (size_t i = 0; i < count; ++i)
        if (parents[i] > lastParent || (parents[i] == lastParent && items[i] > lastItem))
            return i;
    return 0;
}

// Remaining spend for one ancestor pair (the store walks the whole branch):
// the smallest cap remainder wins, uncapped rows do not bind.
inline uint32_t BranchGoldRemaining(uint32_t cap, bool hasCap, uint32_t withdrawn,
    uint32_t parentCap, bool parentHasCap, uint32_t parentWithdrawn)
{
    uint32_t remaining = UINT32_MAX;
    bool capped = false;
    if (hasCap)
    {
        capped = true;
        uint32_t left = withdrawn >= cap ? 0 : cap - withdrawn;
        if (left < remaining)
            remaining = left;
    }
    if (parentHasCap)
    {
        capped = true;
        uint32_t left = parentWithdrawn >= parentCap ? 0 : parentCap - parentWithdrawn;
        if (left < remaining)
            remaining = left;
    }
    return capped ? remaining : UINT32_MAX;
}

// One guild bot that could take the sub-order: what the parent asks each
// candidate, and what the candidate answers about itself.
struct SubOrderCandidate
{
    uint32_t botGuidLow = 0;
    bool online = false;       // candidate resolvable right now
    bool hasOrder = false;     // candidate already carries an order
    bool queuedBusy = false;   // a sub-order already queued for this bot waits
    bool keepHolder = false;   // that order is a keep watch parked at its target
    bool canMake = false;      // knows the recipe and meets the skill
    bool canFarm = false;      // the source finder names a usable farm plan
    bool gathers = false;      // farms it by its own gathering skill (skin, herb, ore, fish)
};

enum class SubOrderPick : uint8_t
{
    None,   // no usable candidate in the guild
    Take,   // a free candidate takes the sub-order now
    Queued, // only busy candidates: keep the request queued for a retry
};

// Depth of the new sub-order in the branch, counting the parent order as 1.
// A replacement of an existing row fails the same way an over-deep branch
// does: the ladder falls back to the need list.
inline size_t SubOrderBranchDepth(size_t parentDepth)
{
    return parentDepth + 1;
}

// Whether a new sub-order may be placed under the parent: inside the limit
// and without repeating the item anywhere up the branch.
inline bool SubOrderDepthAllows(size_t parentDepth, uint32_t maxDepth)
{
    return SubOrderBranchDepth(parentDepth) <= maxDepth;
}

// Whether the item may be requested below the parent: never twice in one
// branch, or the branch would farm its own reagents forever. Sibling rows
// for other items do not block each other: one parent can run several
// sub-orders in parallel, so only the item's own row or queued request
// counts as a duplicate.
inline bool SubOrderLoopFree(uint32_t itemId, uint32_t const* branchItems, size_t branchCount)
{
    if (!itemId || !branchItems)
        return itemId != 0;
    for (size_t i = 0; i < branchCount; ++i)
        if (branchItems[i] == itemId)
            return false;
    return true;
}

// Parallel sub-orders: the parent delegates one component per ladder
// refresh and supplies the rest itself, so several sub-orders run at the
// same time. The next component is the first missing one with no running
// sub-order (or queued request) for it in the branch. Index-based so the
// caller keeps the missing count next to the id; missingCount itself means
// every component is covered and the parent waits on its deliveries.
inline size_t NextUncoveredSubOrderItem(uint32_t const* missingItems, size_t missingCount,
    uint32_t const* coveredItems, size_t coveredCount)
{
    if (!missingItems)
        return missingCount;
    for (size_t i = 0; i < missingCount; ++i)
    {
        if (!missingItems[i])
            continue;
        bool covered = false;
        if (coveredItems)
            for (size_t j = 0; j < coveredCount; ++j)
                if (coveredItems[j] == missingItems[i])
                {
                    covered = true;
                    break;
                }
        if (!covered)
            return i;
    }
    return missingCount;
}


// Rank the free candidates: gatherers beat makers, makers beat farmers, and the first listed of
// equal kind wins so the pick is stable across retries.
inline int SubOrderCandidateRank(SubOrderCandidate const& candidate)
{
    if (!candidate.online || candidate.hasOrder || candidate.queuedBusy)
        return -1;
    // A gatherer beats a maker: crafting a raw material walks the recipe
    // chain under it (Thick Leather from Heavy from Medium from scraps).
    if (candidate.canFarm && candidate.gathers)
        return 3;
    if (candidate.canMake)
        return 2;
    if (candidate.canFarm)
        return 1;
    return -1;
}

// Verdict of a roster scan so far, carried between attempts because one
// attempt only looks at part of the roster: the best free candidate seen in
// any slice, and whether a busy bot that could still take the item was
// seen. The store clears it only when a new full scan starts; dropping the
// busy flag between slices fails a request whose only usable member sits in
// an early slice.
struct SubOrderScanBest
{
    uint32_t guid = 0;         // best free candidate so far
    int rank = -1;             // its rank, -1 while none is free
    bool canMake = false;      // that candidate crafts it (else farms it)
    bool sawBusyUsable = false; // a busy bot could still make or farm it
};

// Fold one evaluated candidate into the running scan: a busy bot that could
// take the item keeps the request worth retrying, a free candidate replaces
// the best only when it ranks higher, so the first of equal rank wins as it
// does inside one slice.
inline void NoteSubOrderCandidate(SubOrderScanBest& best, SubOrderCandidate const& candidate)
{
    if (!candidate.online)
        return;
    if (candidate.hasOrder || candidate.queuedBusy)
    {
        // A keep holder parked at its stock target never frees up: counting
        // it would keep the request queued against a bot that cannot take it.
        if (!candidate.keepHolder && (candidate.canMake || candidate.canFarm))
            best.sawBusyUsable = true;
        return;
    }
    int const rank = SubOrderCandidateRank(candidate);
    if (rank > best.rank)
    {
        best.rank = rank;
        best.guid = candidate.botGuidLow;
        best.canMake = candidate.canMake;
    }
}

// Verdict once the scan has looked at the whole roster: a free candidate is
// placed, busy usable ones keep the request queued for a fresh scan, and no
// usable candidate at all fails it onto the need list. `busyScans` counts
// the busy-only scans in the current streak including this one: the limit
// turns a roster that never frees up into a need-list entry as well.
inline SubOrderPick ClassifySubOrderScan(SubOrderScanBest const& best,
    uint32_t busyScans = 0)
{
    if (best.guid && best.rank >= 0)
        return SubOrderPick::Take;
    if (!best.sawBusyUsable || busyScans >= kSubOrderBusyScanLimit)
        return SubOrderPick::None;
    return SubOrderPick::Queued;
}

// One attempt's share of a roster scan, in guids rather than roster
// positions: the roster is re-read every attempt, so a position lands on a
// different member as soon as someone joins or leaves.
struct SubOrderScanPlan
{
    size_t begin = 0;       // first roster index to look at
    size_t remaining = 0;   // ordered entries left before the scan is complete
    uint32_t startGuid = 0; // member the scan started at
    bool empty = true;      // roster empty: nothing to look at and nothing to retry
};

// Resume point over an ordered roster: the first member after `lastGuid`,
// wrapping once to the roster start. The walk runs forward from there and
// stops once it has gone all the way round to `startGuid` again, so each
// member of the roster is looked at once per scan however the attempt is
// split. A member that joins with a guid below the scan start is looked at
// on the wrap; one that joins between the scan start and the cursor sits in
// ground this scan already covered and is not walked a second time.
// `guids` must be ascending.
inline SubOrderScanPlan PlanSubOrderScan(uint32_t const* guids, size_t count,
    uint32_t lastGuid, uint32_t startGuid)
{
    SubOrderScanPlan plan;
    if (!guids || !count)
        return plan;
    plan.empty = false;
    size_t begin = 0;
    while (begin < count && guids[begin] <= lastGuid)
        ++begin;
    if (begin == count)
        begin = 0;
    size_t startIdx = count;
    if (startGuid)
        for (size_t i = 0; i < count; ++i)
            if (guids[i] == startGuid)
            {
                startIdx = i;
                break;
            }
    if (startIdx == count)
    {
        // A fresh scan starts at the member it resumes from. A scan whose
        // start member left the guild is bounded the same way: the walk
        // still ends instead of re-reading the same tail every attempt.
        // It can take an extra lap over ground an earlier slice already
        // covered (the remainder is the whole roster again), which is
        // cheaper than skipping members the shrunk roster still holds.
        startIdx = begin;
        startGuid = guids[begin];
    }
    plan.begin = begin;
    plan.startGuid = startGuid;
    plan.remaining = begin >= startIdx ? (count - begin) + startIdx : startIdx - begin;
    return plan;
}

// Pre-Give re-check of the candidate a scan carried across slices: the bot
// must still be the same live, same-guild, durable member with no order
// waiting, or Give would REPLACE whatever order it took in the meantime.
struct SubOrderRecheck
{
    bool found = false;     // player resolves now
    bool inWorld = false;   // and is in the world
    bool sameGuild = false; // and still in the parent's guild
    bool durable = false;   // and still a durable order bot
    bool hasOrder = false;  // and carries a running order by the placement rule
};

inline bool SubOrderRecheckPasses(SubOrderRecheck const& state)
{
    return state.found && state.inWorld && state.sameGuild && state.durable && !state.hasOrder;
}

struct SubOrderScanCursor
{
    uint32_t lastGuid = 0;  // member the next attempt resumes after
    uint32_t startGuid = 0; // member this scan started at
    bool complete = false;  // the walk came back around to the start member
};

// Cursor after an attempt that looked at `looked` of the plan's entries: a
// short walk resumes at the last member it looked at, and only a walk that
// covered the plan's remainder finishes the scan.
inline SubOrderScanCursor AdvanceSubOrderScan(uint32_t const* guids, size_t count,
    SubOrderScanPlan const& plan, uint32_t lastGuid, size_t looked)
{
    SubOrderScanCursor cursor;
    cursor.lastGuid = lastGuid;
    cursor.startGuid = plan.startGuid;
    cursor.complete = plan.empty;
    if (plan.empty)
        return cursor;
    size_t const capped = looked < plan.remaining ? looked : plan.remaining;
    if (capped)
        cursor.lastGuid = guids[(plan.begin + capped - 1) % count];
    cursor.complete = capped == plan.remaining;
    return cursor;
}

} // namespace GuildOrders
} // namespace TortoiseBots
