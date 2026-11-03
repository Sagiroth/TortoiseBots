#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

// Pure wording for a sell order that sold nothing (`.bot sell`, the TBM
// "Sell junk" button). The caller gathers the two facts from the bot and
// passes them in, so the rule and the grouping stay testable without the
// server.

namespace ai
{
    // greyItems: grey items in the bags that the default sell filter selects.
    // vendorInReach: a vendor NPC with stock is within interaction range.
    inline std::string SellRefusalReason(std::size_t greyItems, bool vendorInReach)
    {
        if (!greyItems)
            return "nothing grey to sell";
        if (!vendorInReach)
            return "no vendor in reach";
        return "no grey item the bot will sell here";
    }

    // "No bot sold anything: nothing grey to sell; no vendor in reach." Each
    // distinct reason appears once, in first-seen order.
    inline std::string FormatSellRefusal(std::vector<std::string> const& reasons)
    {
        std::vector<std::string> distinct;
        for (std::string const& reason : reasons)
            if (std::find(distinct.begin(), distinct.end(), reason) == distinct.end())
                distinct.push_back(reason);

        std::string text = "No bot sold anything";
        for (std::size_t i = 0; i < distinct.size(); ++i)
            text += (i == 0 ? ": " : "; ") + distinct[i];
        return text + ".";
    }
}
