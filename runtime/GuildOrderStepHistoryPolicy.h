#pragma once

#include "GuildOrderContract.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace GuildOrders
{

// Per-order step history for the addon timeline: one entry per change of
// state, reason, leg or leg item, newest last, bounded. Memory only
// (never persisted, never sent live): the addon asks for it on demand.
inline constexpr std::size_t kOrderStepHistoryMax = 16;

struct OrderStepEntry
{
    uint64_t atSec = 0;
    OrderState state = OrderState::Working;
    OrderReason reason = OrderReason::None;
    SupplyStep step = SupplyStep::None;
    uint32_t itemId = 0;
};

struct OrderStepHistory
{
    std::vector<OrderStepEntry> entries;
    uint32_t dropped = 0; // entries pushed out of the ring, saturating at UINT32_MAX
};

// True when entries is empty or the back entry's tuple differs.
inline bool OrderStepDiffers(OrderStepHistory const& h, OrderState state, OrderReason reason,
    SupplyStep step, uint32_t itemId)
{
    if (h.entries.empty())
        return true;
    OrderStepEntry const& back = h.entries.back();
    return back.state != state || back.reason != reason || back.step != step || back.itemId != itemId;
}

// Appends only when the tuple changed; drops the oldest entry on overflow
// and counts it in dropped (which saturates, never wraps).
inline void NoteOrderStep(OrderStepHistory& h, OrderStepEntry const& entry)
{
    if (!OrderStepDiffers(h, entry.state, entry.reason, entry.step, entry.itemId))
        return;
    h.entries.push_back(entry);
    if (h.entries.size() > kOrderStepHistoryMax)
    {
        h.entries.erase(h.entries.begin());
        if (h.dropped < UINT32_MAX)
            ++h.dropped;
    }
}

// D7 wire: BEGIN, one ORDER_STEP per entry (oldest first), END. Age is the
// server's own subtraction, clamped at 0 for a client clock ahead of it.
inline std::vector<std::string> FormatOrderStepLines(std::string const& safeBotName,
    OrderStepHistory const& h, uint64_t nowSec)
{
    std::vector<std::string> lines;
    lines.push_back("TBM:ORDER_STEPS_BEGIN|" + safeBotName + "|" +
        std::to_string(h.entries.size()) + "|" + std::to_string(h.dropped));
    for (OrderStepEntry const& entry : h.entries)
    {
        uint64_t age = nowSec > entry.atSec ? nowSec - entry.atSec : 0;
        lines.push_back("TBM:ORDER_STEP|" + safeBotName + "|" + std::to_string(age) + "|" +
            StateToken(entry.state) + "|" + ReasonToken(entry.reason) + "|" +
            SupplyStepToken(entry.step) + "|" + std::to_string(entry.itemId));
    }
    lines.push_back("TBM:ORDER_STEPS_END|" + safeBotName);
    return lines;
}

} // namespace GuildOrders
} // namespace TortoiseBots
