#pragma once

// What a player feels: the server handles a player's packets once per world
// tick, so an action waits for the rest of the tick it lands in. The client's
// latency meter never shows this (the core answers CMSG_PING on the network
// thread), so the dashboard derives it from the world tick lengths instead.
//
// An action arriving at a random moment lands in a tick with probability
// proportional to the tick's length and then waits for the remainder of it:
// P(wait > x) = sum(max(0, tick - x)) / sum(tick). p50/p95 are the waits that
// half / 95 % of player actions stay under. Checked against a headless probe
// client on the live realm (2000 bots); see tools/observability/README.md.
//
// World-thread only. Standalone and testable (tools/test_player_lag_window.cpp).

#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

namespace TortoiseBots
{

class PlayerLagWindow
{
public:
    struct Stats
    {
        uint32_t avgMs = 0;   // plain mean tick length
        uint32_t worstMs = 0; // longest tick
        uint32_t p50Ms = 0;   // median wait of a player action
        uint32_t p95Ms = 0;   // 95th percentile wait of a player action
    };

    explicit PlayerLagWindow(uint32_t windowMs = 30000) : m_windowMs(windowMs) {}

    void Add(uint32_t tickMs)
    {
        if (tickMs == 0)
            return;
        m_ticks.push_back(tickMs);
        m_totalMs += tickMs;
        while (m_ticks.size() > 1 && m_totalMs - m_ticks.front() >= m_windowMs)
        {
            m_totalMs -= m_ticks.front();
            m_ticks.pop_front();
        }
    }

    Stats Compute() const
    {
        Stats s;
        if (m_ticks.empty())
            return s;

        std::vector<uint32_t> sorted(m_ticks.begin(), m_ticks.end());
        std::sort(sorted.begin(), sorted.end());
        s.avgMs = static_cast<uint32_t>(m_totalMs / sorted.size());
        s.worstMs = sorted.back();
        s.p50Ms = WaitPercentile(sorted, 0.50);
        s.p95Ms = WaitPercentile(sorted, 0.95);
        return s;
    }

private:
    // Smallest wait x with P(wait > x) <= 1 - fraction (binary search on x).
    uint32_t WaitPercentile(std::vector<uint32_t> const& sorted, double fraction) const
    {
        double const allowed = (1.0 - fraction) * static_cast<double>(m_totalMs);
        uint32_t lo = 0, hi = sorted.back();
        while (lo < hi)
        {
            uint32_t const mid = lo + (hi - lo) / 2;
            uint64_t over = 0;
            for (auto it = std::upper_bound(sorted.begin(), sorted.end(), mid); it != sorted.end(); ++it)
                over += *it - mid;
            if (static_cast<double>(over) <= allowed)
                hi = mid;
            else
                lo = mid + 1;
        }
        return lo;
    }

    uint32_t m_windowMs;
    uint64_t m_totalMs = 0;
    std::deque<uint32_t> m_ticks;
};

} // namespace TortoiseBots
