#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// Hand a delivery attempt from the map-thread bank action to the world
// thread. The core guild bank keeps the acting player in member fields and
// writes the database without a lock, so HandleAddonMessages must run where
// player bank messages run: the world thread. The map thread only copies
// bot guid + item id in; the world drain re-resolves bot, guild and banker,
// deposits, mails leftovers and updates the order store.
//
// Plain data and the standard library only, so the delivery policy test
// exercises the queue standalone.
struct PendingDelivery
{
    uint32_t botGuidLow = 0;
    uint32_t itemId = 0;
};

class DeliveryQueue
{
public:
    static DeliveryQueue& Instance()
    {
        static DeliveryQueue queue;
        return queue;
    }

    // Any thread. One request per bot pending; a second one while the first
    // still waits is dropped.
    bool Enqueue(uint32_t botGuidLow, uint32_t itemId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (PendingDelivery const& pending : m_pending)
            if (pending.botGuidLow == botGuidLow)
                return false;
        m_pending.push_back(PendingDelivery{ botGuidLow, itemId });
        return true;
    }

    // World thread. Takes everything queued so far, in arrival order.
    std::vector<PendingDelivery> TakeAll()
    {
        std::vector<PendingDelivery> batch;
        std::lock_guard<std::mutex> lock(m_mutex);
        batch.swap(m_pending);
        return batch;
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.clear();
    }

    size_t PendingCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pending.size();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<PendingDelivery> m_pending;
};

} // namespace GuildOrders
} // namespace TortoiseBots
