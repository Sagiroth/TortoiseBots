#pragma once

// The item instances a dungeon run GAVE to its bots (spec item-leg-skip D4-D7,
// review round 2 Codex 1): the exact item GUIDs (low part) the run created,
// recorded in the crash ledger ai_run_given_item before the bot could use
// them. The run's take-back destroys exactly these instances (never a bot's
// own copy of the same item), and the bot paths that move an item to someone
// else (GiveItemAction, SellAction) refuse them. The
// core refuses trade, mail and auction of them by the soulbound flag the give
// sets. World thread only. Plain std, includable without core.

#include <cstdint>
#include <unordered_set>

namespace TortoiseBots
{
namespace RunGivenItems
{

inline std::unordered_set<std::uint32_t>& Guids()
{
    static std::unordered_set<std::uint32_t> guids;
    return guids;
}

inline void Add(std::uint32_t itemGuidLow)
{
    if (itemGuidLow)
        Guids().insert(itemGuidLow);
}

inline void Erase(std::uint32_t itemGuidLow)
{
    Guids().erase(itemGuidLow);
}

// True: the run gave this item instance; it never leaves the bot but by the
// run's own take-back.
inline bool Has(std::uint32_t itemGuidLow)
{
    return itemGuidLow && Guids().count(itemGuidLow) != 0;
}

} // namespace RunGivenItems
} // namespace TortoiseBots
