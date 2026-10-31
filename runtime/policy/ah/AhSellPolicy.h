#pragma once

#include <cstdint>
#include <unordered_set>

namespace ai
{
    inline uint32_t const kAhSellBlacklistDefault = 18256;

    inline bool AhItemSellBlacklisted(uint32_t itemId, std::unordered_set<uint32_t> const& blacklist)
    {
        return blacklist.count(itemId) > 0;
    }

    inline bool AhUnlimitedVendorStock(bool soldByAnyVendor, bool limitedMaxCount)
    {
        return soldByAnyVendor && !limitedMaxCount;
    }
} // namespace ai
