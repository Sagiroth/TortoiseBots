#pragma once
#include <cstdint>
namespace TortoiseBots
{
struct ConsumableProvisionClock
{
    bool alive = false;
    std::uint32_t remaining = 0;
    bool Tick(bool isAlive, std::uint32_t elapsed)
    {
        if (!isAlive) { alive = false; remaining = 0; return false; }
        if (!alive) { alive = true; remaining = 0; }
        remaining = elapsed >= remaining ? 0 : remaining - elapsed;
        return remaining == 0;
    }
};
}
