#pragma once
#include <cstdint>

namespace ai
{
    inline bool LoathebSporeProtected(bool enabled, bool inNaxx, bool encounterActive,
                                       std::uint32_t entry, bool ordered)
    {
        return enabled && inNaxx && encounterActive && entry == 16286 && !ordered;
    }
}
