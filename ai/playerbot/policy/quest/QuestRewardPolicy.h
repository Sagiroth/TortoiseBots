#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace ai
{
    // Quest-reward score tiebreak (questreward, AG-3/RPG-A1).
    //
    // BestRewards ranks choice rewards by item usage only, so two EQUIP (or
    // two BAD_EQUIP) rewards tie and the caller takes the first in vendor
    // order - which is arbitrary stat-wise. The caller scores each tied
    // candidate by its gain over the item worn in the slot it would take
    // (full weight into an empty slot), so the biggest upgrade wins rather
    // than the piece that scores highest on its own: a chest that barely
    // beats the worn chest must lose to a weapon far better than the worn
    // weapon. This rule picks the winner from (index, gain) pairs; a gain
    // can be negative.
    //
    // Pure decision rule, no core includes: the caller does the weight
    // reads, the rule only picks. Strictly-greater keeps vendor order on an
    // exact tie (same idiom as VendorBuyPolicy).

    // Winner index among tied candidates: highest gain wins; an exact tie
    // keeps the earlier (vendor-order) candidate. Empty input yields 0.
    inline std::uint32_t QuestRewardTiebreak(
        std::vector<std::pair<std::uint32_t, std::int64_t>> const& scored)
    {
        std::uint32_t best = 0;
        std::int64_t bestScore = 0;
        bool first = true;
        for (auto const& candidate : scored)
        {
            if (first || candidate.second > bestScore)
            {
                best = candidate.first;
                bestScore = candidate.second;
                first = false;
            }
        }
        return best;
    }
}
