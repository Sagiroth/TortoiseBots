#pragma once

#include "GuildOrderContract.h"

#include <cstdint>
#include <string>
#include <vector>

// Read-only plan behind `.bot order <bot> preview`: what the bot would do
// for one item and amount, using the same planner verdicts the give path
// uses (PlanGuildOrderSources) and the same whole-tree reagent walk the
// supply ladder uses (CollectReagentTotals). Header-only and free of core
// types so the shape stays unit tested on its own
// (tools/test_guild_order_preview_policy.cpp): the command feeds this header
// plain counts it already read from the bot's bags, mailbox, bank and the
// auction mirror.
namespace TortoiseBots
{
namespace GuildOrders
{

// One reagent row of the preview: the whole-tree need, what the bot holds
// in each place the real order counts, and how the ladder would source the
// rest. AH rows only say the counter may have it (the order buys when
// something fits and it has the gold); farm rows carry the planned zone
// name ("-" when none: zone names travel as text because the addon holds
// no zone-id map).
struct PreviewReagent
{
    uint32_t itemId = 0;
    uint32_t required = 0;
    uint32_t inBags = 0;
    uint32_t inMail = 0;
    uint32_t inBank = 0;
    uint32_t farmCount = 0;
    std::string farmZone;
    uint32_t ahCount = 0;
    uint32_t vendorCount = 0;
    uint32_t subCount = 0;
};

// Missing after every source the order would try: bags, mailbox, bank, the
// auction house, the bot itself and other guild bots. Only this lands on
// the player's need list.
inline uint32_t PreviewNeedCount(uint32_t required, uint32_t inBags, uint32_t inMail,
    uint32_t inBank, uint32_t ahCount, uint32_t farmCount, uint32_t vendorCount,
    uint32_t subCount)
{
    uint64_t covered = uint64_t(inBags) + inMail + inBank + ahCount + farmCount +
        vendorCount + subCount;
    return covered >= required ? 0 : uint32_t(uint64_t(required) - covered);
}

// Rough worked minutes from expected kills or gathers: one minute per five
// actions. Craft-only rows (no farm leg) read as a short bench job.
inline uint32_t PreviewMinutesForKills(double expectedKills, bool hasFarmLeg)
{
    if (!hasFarmLeg)
        return 10;
    if (expectedKills <= 0.0)
        return 10;
    uint32_t minutes = uint32_t(expectedKills / 5.0) + 10;
    return minutes < 10 ? 10 : minutes;
}

// One TBM:ORDER_PREVIEW reagent field: item, need, held, farm, zone, AH,
// vendor and sub-order counts, then what the player must still send. The
// zone is already sanitized (no wire separators); empty means none.
inline std::string PreviewReagentToken(PreviewReagent const& row)
{
    std::string zone = row.farmZone.empty() ? "-" : row.farmZone;
    return std::to_string(row.itemId) + ":" + std::to_string(row.required) + ":" +
        std::to_string(row.inBags) + ":" + std::to_string(row.inMail) + ":" +
        std::to_string(row.inBank) + ":" + std::to_string(row.farmCount) + ":" +
        zone + ":" + std::to_string(row.ahCount) + ":" +
        std::to_string(row.vendorCount) + ":" + std::to_string(row.subCount) + ":" +
        std::to_string(PreviewNeedCount(row.required, row.inBags, row.inMail, row.inBank,
            row.ahCount, row.farmCount, row.vendorCount, row.subCount));
}

// Reagent list field: one token per row, ";" between rows, "-" when empty.
inline std::string PreviewReagentsToken(std::vector<PreviewReagent> const& rows)
{
    std::string out;
    for (PreviewReagent const& row : rows)
    {
        if (!row.itemId || !row.required)
            continue;
        if (!out.empty())
            out += ';';
        out += PreviewReagentToken(row);
    }
    return out.empty() ? "-" : out;
}

} // namespace GuildOrders
} // namespace TortoiseBots
