#pragma once

// Round-robin rotation for the random-pool AI pass (BotManager::UpdateBots).
//
// The rotation is the fairness mechanism behind the per-tick pool budget: a
// pass resumes where the previous tick stopped, so a bot that misses a pass is
// served on a later one instead of being skipped. The caller owns the guid
// order and rebuilds it with Refresh() when the pool membership changes; this
// class owns only the cursor and the budget check, which keeps it out of the
// core and testable on its own (tools/test_pool_pass_rotation.cpp).
//
// World-thread only, like the rest of BotManager.

#include <cstdint>
#include <vector>

namespace TortoiseBots
{

class PoolPassRotation
{
public:
    // Keep the current order while the live membership is unchanged: equal
    // size plus "every stored guid is still in the pool" proves the sets are
    // equal, because both hold distinct guids drawn from the same live pool.
    // A rebuild keeps the rotation's place: it resumes at the bot the cursor
    // pointed at, or at the same index when that bot left. Restarting at 0
    // starved the tail: with 2000 bots and ~50 served per tick, logins
    // changed the pool every tick and the tail never got an update.
    template <typename InPoolFn>
    void Refresh(std::vector<uint32_t> const& livePool, InPoolFn stillInPool)
    {
        if (m_order.size() != livePool.size())
        {
            Rebuild(livePool);
            return;
        }

        for (uint32_t guidLow : m_order)
        {
            if (!stillInPool(guidLow))
            {
                Rebuild(livePool);
                return;
            }
        }
    }

    uint32_t Size() const { return static_cast<uint32_t>(m_order.size()); }

    // Visit bots from the cursor, resuming where the previous call stopped, and
    // stop early when the budget is spent. `nowUs` is a monotonic microsecond
    // clock; the budget is checked before starting the next bot (and only
    // while budgetActive), so a call never starts new work over budget. Returns
    // the number of bots visited and sets budgetHit when the stop left work
    // for the next tick. Without an active budget the whole pool is visited
    // and the cursor ends where it started.
    template <typename NowUsFn, typename UpdateFn>
    uint32_t Run(bool budgetActive, uint64_t budgetUs, NowUsFn nowUs, UpdateFn update, bool& budgetHit)
    {
        uint32_t const size = Size();
        budgetHit = false;
        if (size == 0)
        {
            m_cursor = 0;
            return 0;
        }
        if (m_cursor >= size)
            m_cursor = 0;

        uint64_t const startUs = budgetActive ? nowUs() : 0;
        uint32_t processed = 0;
        for (uint32_t remaining = size; remaining > 0; --remaining)
        {
            // Budget already spent: leave the cursor on this bot so the next
            // pass resumes here instead of starting another over-budget update.
            if (budgetActive && nowUs() - startUs >= budgetUs)
            {
                budgetHit = true;
                break;
            }
            uint32_t const guidLow = m_order[m_cursor];
            m_cursor = (m_cursor + 1 == size) ? 0 : m_cursor + 1;
            update(guidLow);
            ++processed;
        }
        return processed;
    }

private:
    void Rebuild(std::vector<uint32_t> const& livePool)
    {
        uint32_t const resumeGuid = m_cursor < m_order.size() ? m_order[m_cursor] : 0;
        m_order.assign(livePool.begin(), livePool.end());
        for (uint32_t i = 0; i < m_order.size(); ++i)
        {
            if (m_order[i] == resumeGuid)
            {
                m_cursor = i;
                return;
            }
        }
        if (m_cursor >= m_order.size())
            m_cursor = 0;
    }

    std::vector<uint32_t> m_order;
    uint32_t m_cursor = 0;
};

} // namespace TortoiseBots
