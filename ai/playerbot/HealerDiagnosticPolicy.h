#pragma once
#include <cstdint>

namespace ai
{
// Separate budgets for snapshots and failed checks. No per-reason bypass.
class HealerDiagnosticClock
{
public:
    bool Due(std::uint32_t now)
    {
        if (started && std::uint32_t(now - last) < 10000)
            return false;
        started = true;
        last = now;
        return true;
    }
private:
    bool started = false;
    std::uint32_t last = 0;
};
}
