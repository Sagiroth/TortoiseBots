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
    // order - which is arbitrary stat-wise. Donor mod-playerbots breaks the
    // tie by stat-weight score (TalkToQuestGiverAction / NewRpgBaseAction);
    // here the caller scores each tied candidate with the same
    // GetLiveStatWeight the buy/equip paths use and this rule picks the
    // winner from (index, weight) pairs.
    //
    // Pure decision rule, no core includes: the caller does the weight
    // reads, the rule only picks. Strictly-greater keeps vendor order on an
    // exact tie (same idiom as VendorBuyPolicy).

    // Winner index among tied candidates: highest weight wins; an exact tie
    // keeps the earlier (vendor-order) candidate. Empty input yields 0.
    inline std::uint32_t QuestRewardTiebreak(
        std::vector<std::pair<std::uint32_t, std::uint32_t>> const& scored)
    {
        std::uint32_t best = 0;
        std::uint32_t bestScore = 0;
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
