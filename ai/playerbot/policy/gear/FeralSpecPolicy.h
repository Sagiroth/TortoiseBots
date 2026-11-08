#pragma once

#include <cstdint>

namespace ai
{
    // Resolves whether a druid with Feral talents uses bear ("feraltank")
    // or cat ("feraldps") stat weights. Cat Form is unavailable before level
    // 20, so low-level ferals always resolve as tank. At level 20+, the bot's
    // active role decides.
    inline const char* ResolveDruidFeralSpecName(uint32_t level, bool isTank)
    {
        if (level <= 19 || isTank)
            return "feraltank";
        return "feraldps";
    }
}
