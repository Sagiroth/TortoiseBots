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
 * Ported from mod-ah-bot-plus AuctionHouseBot.cpp@f6858329.
 */
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace TortoiseBots
{

class AhBotService
{
public:
    static AhBotService& Instance();

    void Update(uint32_t diff);
    void Reload();
    uint32_t Empty();
    void ForceUpdate();
    std::string GetStatus() const;

private:
    AhBotService() = default;
    ~AhBotService() = default;

    struct PendingSellHouse
    {
        uint32_t houseId = 0;
        uint32_t remaining = 0;
    };

    struct PendingBuyHouse
    {
        uint32_t houseId = 0;
        uint32_t remaining = 0;
        std::vector<uint32_t> candidateIds;
    };

    void EnsureInitialized();
    void LoadScheduleConfig();
    void EnsureVendorPricesLoaded();
    void EnqueueLegacyOwner0Cleanup();
    void EnqueueOwnExpiringListings();
    void EnqueueSellCycle();
    void EnqueueBuyCycle();
    void RunBudgetedSlice();

    bool StepLegacyCleanup();
    bool StepExpireRemoval();
    bool StepSellOne();
    bool StepBuyOne();

    std::vector<uint32_t> EnabledHouseIds() const;
    uint32_t RollIntervalMs(std::string const& raw) const;

    bool m_initialized = false;
    bool m_poolLoaded = false;
    bool m_vendorPricesLoaded = false;
    bool m_legacyCleanupDone = false;

    uint32_t m_sellElapsedMs = 0;
    uint32_t m_sellIntervalMs = 60000;
    uint32_t m_buyElapsedMs = 0;
    uint32_t m_buyIntervalMs = 60000;
    uint32_t m_expireSweepElapsedMs = 0;

    std::deque<uint32_t> m_pendingLegacyCleanupIds;
    std::deque<uint32_t> m_pendingExpireIds;
    std::deque<PendingSellHouse> m_pendingSell;
    std::deque<PendingBuyHouse> m_pendingBuy;

    std::unordered_map<uint32_t, uint32_t> m_vendorSellPrices;

    uint64_t m_lastSliceUs = 0;
    uint64_t m_maxSliceUs = 0;
    uint64_t m_totalListed = 0;
    uint64_t m_totalBids = 0;
    uint64_t m_totalBuyouts = 0;
    uint64_t m_totalExpiredClean = 0;
    uint64_t m_totalLegacyCleaned = 0;
};

} // namespace TortoiseBots
