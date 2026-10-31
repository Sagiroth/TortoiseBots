#pragma once

#include "GuildOrderContract.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

struct PendingSupply
{
    uint32_t botGuidLow = 0;
};

class SupplyQueue
{
public:
    static SupplyQueue& Instance()
    {
        static SupplyQueue queue;
        return queue;
    }

    bool Enqueue(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (PendingSupply const& pending : m_pending)
            if (pending.botGuidLow == botGuidLow)
                return false;
        m_pending.push_back(PendingSupply{ botGuidLow });
        return true;
    }

    std::vector<PendingSupply> TakeAll()
    {
        std::vector<PendingSupply> batch;
        std::lock_guard<std::mutex> lock(m_mutex);
        batch.swap(m_pending);
        return batch;
    }

    void Remove(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
            [botGuidLow](PendingSupply const& pending) { return pending.botGuidLow == botGuidLow; }), m_pending.end());
    }

private:
    std::mutex m_mutex;
    std::vector<PendingSupply> m_pending;
};

} // namespace GuildOrders
} // namespace TortoiseBots
