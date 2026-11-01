#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// One banker visit that repays unspent guild gold. The bank action enqueues
// from the map thread; the world-thread refund drain resolves the order and
// the banker, then deposits through the same GuildBank::HandleAddonMessages
// path as the existing withdrawals.
struct PendingRefund
{
    uint32_t botGuidLow = 0;
};

class RefundQueue
{
public:
    static RefundQueue& Instance()
    {
        static RefundQueue queue;
        return queue;
    }

    bool Enqueue(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (PendingRefund const& pending : m_pending)
            if (pending.botGuidLow == botGuidLow)
                return false;
        m_pending.push_back(PendingRefund{ botGuidLow });
        return true;
    }

    std::vector<PendingRefund> TakeAll()
    {
        std::vector<PendingRefund> batch;
        std::lock_guard<std::mutex> lock(m_mutex);
        batch.swap(m_pending);
        return batch;
    }

    void Remove(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
            [botGuidLow](PendingRefund const& pending) { return pending.botGuidLow == botGuidLow; }), m_pending.end());
    }

private:
    std::mutex m_mutex;
    std::vector<PendingRefund> m_pending;
};

} // namespace GuildOrders
} // namespace TortoiseBots
