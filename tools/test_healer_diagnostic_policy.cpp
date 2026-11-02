#include "../ai/playerbot/HealerDiagnosticPolicy.h"
#include <cassert>
#include <cstdint>
#include <limits>
int main()
{
    ai::HealerDiagnosticClock clock;
    assert(clock.Due(0));
    for (std::uint32_t now = 0; now < 10000; ++now)
        assert(!clock.Due(now));
    assert(clock.Due(10000));
    assert(!clock.Due(10001));
    assert(clock.Due(60000)); // No burst to catch up missed windows.
    assert(!clock.Due(60000));
    ai::HealerDiagnosticClock wrap;
    std::uint32_t const start = std::numeric_limits<std::uint32_t>::max() - 5000;
    assert(wrap.Due(start));
    assert(!wrap.Due(start + 9999));
    assert(wrap.Due(start + 10000));
    ai::HealerDiagnosticClock other;
    assert(other.Due(60000)); // Budgets are per bot/event, not global.
}
