#pragma once

// Sub-order picking rules for guild orders: which guild bot makes or farms an
// item the parent cannot get itself. Header-only and free of core types so
// the rules stay unit tested on their own
// (tools/test_guild_order_sub_order_policy.cpp). The store feeds this header
// plain candidate verdicts it already computed per bot; the header only
// ranks and gates them.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// Sub-orders on by default; workers B (bank) and the ladder share the shape.
inline constexpr uint32_t kSubOrderDefaultDepth = 3;

// One guild bot that could take the sub-order: what the parent asks each
// candidate, and what the candidate answers about itself.
struct SubOrderCandidate
{
    uint32_t botGuidLow = 0;
    bool online = false;       // candidate resolvable right now
    bool hasOrder = false;     // candidate already carries an order
    bool queuedBusy = false;   // a sub-order already queued for this bot waits
    bool canMake = false;      // knows the recipe and meets the skill
    bool canFarm = false;      // the source finder names a usable farm plan
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

// Rank the free candidates: makers beat farmers, and the first listed of
// equal kind wins so the pick is stable across retries.
inline int SubOrderCandidateRank(SubOrderCandidate const& candidate)
{
    if (!candidate.online || candidate.hasOrder || candidate.queuedBusy)
        return -1;
    if (candidate.canMake)
        return 2;
    if (candidate.canFarm)
        return 1;
    return -1;
}

// Index of the chosen candidate, or candidateCount when none is free.
// queuedOut is set when only busy candidates remain: the request stays
// queued for a later retry instead of landing on the need list.
inline size_t PickSubOrderCandidate(SubOrderCandidate const* candidates, size_t candidateCount,
    bool& queuedOut)
{
    queuedOut = false;
    if (!candidates || candidateCount == 0)
        return candidateCount;
    size_t best = candidateCount;
    int bestRank = -1;
    bool sawBusy = false;
    for (size_t i = 0; i < candidateCount; ++i)
    {
        SubOrderCandidate const& candidate = candidates[i];
        if (!candidate.online)
            continue;
        if (candidate.hasOrder || candidate.queuedBusy)
        {
            if (candidate.canMake || candidate.canFarm)
                sawBusy = true;
            continue;
        }
        int rank = SubOrderCandidateRank(candidate);
        if (rank > bestRank)
        {
            bestRank = rank;
            best = i;
        }
    }
    if (best == candidateCount && sawBusy)
        queuedOut = true;
    return best;
}

inline SubOrderPick ClassifySubOrderPick(size_t picked, size_t candidateCount, bool queued)
{
    if (picked < candidateCount)
        return SubOrderPick::Take;
    return queued ? SubOrderPick::Queued : SubOrderPick::None;
}

} // namespace GuildOrders
} // namespace TortoiseBots
