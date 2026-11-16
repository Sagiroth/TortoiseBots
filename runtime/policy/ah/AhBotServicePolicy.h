/*
 * Copyright (C) 2008-2010 Trinity <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2023+ Nathan Handley <https://github.com/NathanHandley>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Pure policy for AhBotService (ported from mod-ah-bot-plus AuctionHouseBot.cpp@f6858329).
 */
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>

namespace ai
{

struct AhBotCycleRange
{
    uint32_t min = 1;
    uint32_t max = 1;
};

inline bool ParseUint32Token(std::string const& raw, uint32_t& out)
{
    size_t first = 0;
    while (first < raw.size() && std::isspace(static_cast<unsigned char>(raw[first])))
        ++first;
    size_t last = raw.size();
    while (last > first && std::isspace(static_cast<unsigned char>(raw[last - 1])))
        --last;
    if (first >= last)
        return false;

    uint64_t val = 0;
    for (size_t i = first; i < last; ++i)
    {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        if (!std::isdigit(c))
            return false;
        val = val * 10 + (c - '0');
        if (val > std::numeric_limits<uint32_t>::max())
        {
            out = std::numeric_limits<uint32_t>::max();
            return true;
        }
    }
    out = static_cast<uint32_t>(val);
    return true;
}

inline AhBotCycleRange ParseAhBotRange(std::string const& raw, uint32_t defaultVal,
    uint32_t minAllowed = 1, uint32_t maxAllowed = 1440)
{
    auto clampVal = [minAllowed, maxAllowed](uint32_t v) -> uint32_t
    {
        return std::min(maxAllowed, std::max(minAllowed, v));
    };

    uint32_t fallback = clampVal(defaultVal);
    size_t colon = raw.find(':');
    if (colon == std::string::npos)
    {
        uint32_t val = 0;
        if (!ParseUint32Token(raw, val))
            return { fallback, fallback };
        uint32_t clamped = clampVal(val);
        return { clamped, clamped };
    }

    uint32_t lo = 0;
    uint32_t hi = 0;
    if (!ParseUint32Token(raw.substr(0, colon), lo) || !ParseUint32Token(raw.substr(colon + 1), hi))
        return { fallback, fallback };

    lo = clampVal(lo);
    hi = clampVal(hi);
    if (hi < lo)
        hi = lo;
    return { lo, hi };
}

inline uint32_t ComputeAhBotSellCount(uint32_t currentCount, uint32_t minItems,
    uint32_t maxItems, uint32_t itemsPerCycle)
{
    if (maxItems == 0 || itemsPerCycle == 0)
        return 0;
    if (currentCount >= minItems || currentCount >= maxItems)
        return 0;
    return std::min(maxItems - currentCount, itemsPerCycle);
}

inline bool ShouldRemoveOwnAhBotListing(bool isSellerOwner, uint32_t currentBidder,
    int64_t expireTime, int64_t now, int64_t earlyBufferSec = 60)
{
    if (!isSellerOwner || currentBidder != 0)
        return false;
    return expireTime <= (now + earlyBufferSec);
}

inline bool IsLegacySyntheticOwner(uint32_t ownerGuidLow)
{
    return ownerGuidLow == 0;
}

enum class AhBotBuyDecision : uint8_t
{
    Skip = 0,
    Bid,
    Buyout
};

struct AhBotBuyEval
{
    AhBotBuyDecision action = AhBotBuyDecision::Skip;
    uint32_t amount = 0;
};

inline AhBotBuyEval EvaluateAhBotBuyerCandidate(
    uint32_t buyout,
    uint32_t currentBid,
    uint32_t startbid,
    uint32_t outbidIncrement,
    uint64_t fairPerUnit,
    uint32_t stackCount,
    uint32_t vendorSellPrice,
    bool alwaysBidMax,
    bool preventOverpayVendor)
{
    if (fairPerUnit == 0 || stackCount == 0)
        return {};

    constexpr uint64_t kMaxCopper = 0x7fffffffULL;
    uint64_t willingTotal = 0;
    if (fairPerUnit > kMaxCopper / stackCount)
        willingTotal = kMaxCopper;
    else
        willingTotal = fairPerUnit * stackCount;

    bool doBuyout = false;
    bool doBid = false;
    uint64_t calcBidAmount = 0;

    if (buyout != 0 && buyout < willingTotal)
    {
        doBuyout = true;
    }
    else if (currentBid == 0 && startbid <= willingTotal)
    {
        doBid = true;
        calcBidAmount = alwaysBidMax ? willingTotal : uint64_t(startbid);
    }
    else if (currentBid != 0 && (uint64_t(currentBid) + outbidIncrement) < willingTotal)
    {
        doBid = true;
        calcBidAmount = alwaysBidMax ? willingTotal : (uint64_t(currentBid) + outbidIncrement);
    }

    if (doBid && buyout != 0 && calcBidAmount >= buyout)
    {
        doBid = false;
        doBuyout = true;
    }

    if (preventOverpayVendor && vendorSellPrice > 0)
    {
        if (doBuyout && buyout > vendorSellPrice)
            doBuyout = false;
        if (doBid && calcBidAmount > vendorSellPrice)
            doBid = false;
    }

    if (doBuyout && buyout > 0)
        return { AhBotBuyDecision::Buyout, buyout };

    if (doBid && calcBidAmount > 0)
        return { AhBotBuyDecision::Bid, static_cast<uint32_t>(std::min(calcBidAmount, kMaxCopper)) };

    return {};
}

} // namespace ai
