#pragma once

#include <algorithm>
#include <cstdint>

namespace ai
{
    inline uint32_t const kAhSellBlacklistDefault = 18256;

    // Owner-editable blocklist (AiPlayerbot.AhSellBlacklist): exact id match.
    // Works on any id container so bot inventory, vendor-flip and synthetic
    // supply checks share this one rule.
    template <typename Container>
    inline bool AhItemSellBlacklisted(uint32_t itemId, Container const& blacklist)
    {
        return std::find(blacklist.begin(), blacklist.end(), itemId) != blacklist.end();
    }

    // Owner rule: an item has unlimited vendor stock when ANY vendor sells it
    // without a stock limit, even if other vendors limit it (Imbued Vial:
    // 60 unlimited rows alongside 14 limited ones still counts).
    inline bool AhUnlimitedVendorStock(bool soldByAnyVendorWithoutLimit)
    {
        return soldByAnyVendorWithoutLimit;
    }
} // namespace ai
