#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// One banker visit that pays collected relist value back to the guild bank.
// The bank action enqueues a bot guid from the map thread; the world-thread
// relist drain re-resolves the bot, banker and guild, deposits through the
// same GuildBank::HandleAddonMessages path as the order deliveries, and keeps
// every row the bank did not take for the next trip.
struct PendingRelistDeposit
{
    uint32_t botGuidLow = 0;
};

class RelistQueue
{
public:
    static RelistQueue& Instance()
    {
        static RelistQueue queue;
        return queue;
    }

    bool Enqueue(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (PendingRelistDeposit const& pending : m_pending)
            if (pending.botGuidLow == botGuidLow)
                return false;
        m_pending.push_back(PendingRelistDeposit{ botGuidLow });
        return true;
    }

    std::vector<PendingRelistDeposit> TakeAll()
    {
        std::vector<PendingRelistDeposit> batch;
        std::lock_guard<std::mutex> lock(m_mutex);
        batch.swap(m_pending);
        return batch;
    }

    void Remove(uint32_t botGuidLow)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
            [botGuidLow](PendingRelistDeposit const& pending) { return pending.botGuidLow == botGuidLow; }),
            m_pending.end());
    }

private:
    std::mutex m_mutex;
    std::vector<PendingRelistDeposit> m_pending;
};

} // namespace GuildOrders
} // namespace TortoiseBots
