#pragma once

#include <cstdint>

// Craft-order arithmetic. Pure rules over spell/item numbers, kept apart from
// the engine so they stay unit tested on their own
// (tools/test_craft_order_policy.cpp).
//
// Yield: the server creates CalculateSpellDamage items per cast - base points
// plus a dice roll between base dice and die sides (Object.cpp). Crafting
// plans with the guaranteed minimum, never the average, so the bot always
// casts enough.

namespace TortoiseBots
{
namespace CraftOrderPolicy
{

// Full reagent shortfall for `casts` casts: (need - have), floored at 0.
inline uint32_t ReagentShortfall(uint32_t perCast, uint32_t casts, uint32_t have)
{
    uint64_t need = uint64_t(perCast) * casts;
    return need > have ? uint32_t(need - have) : 0;
}

// Guaranteed items per cast: the roll can only add on top of this.
inline uint32_t MinYieldPerCast(int32_t basePoints, int32_t baseDice, int32_t dieSides)
{
    int32_t roll = baseDice < dieSides ? baseDice : dieSides;
    int32_t yield = basePoints + roll;
    return yield < 1 ? 1 : uint32_t(yield);
}

// Casts needed for `remaining` more items at the guaranteed yield.
inline uint32_t CastsForRemaining(uint32_t remaining, uint32_t yieldPerCast)
{
    if (remaining == 0)
        return 0;
    if (yieldPerCast == 0)
        return remaining;
    return (remaining + yieldPerCast - 1) / yieldPerCast;
}

// A cast that fails for lack of bag room still eats reagents, so the bot
// only crafts while the worst case fits: every cast yielding its maximum.
inline uint32_t MaxYieldPerCast(int32_t basePoints, int32_t baseDice, int32_t dieSides)
{
    int32_t roll = baseDice > dieSides ? baseDice : dieSides;
    int32_t yield = basePoints + roll;
    return yield < 1 ? 1 : uint32_t(yield);
}

inline bool FitsInFreeSlots(uint32_t casts, uint32_t maxYieldPerCast, uint32_t stackSize,
                            uint32_t freeSlots)
{
    if (casts == 0)
        return true;
    uint64_t worst = uint64_t(casts) * maxYieldPerCast;
    uint64_t slots = worst / stackSize + (worst % stackSize ? 1 : 0);
    return slots <= freeSlots;
}

// Largest batch (<= casts) whose worst case fits: every cast yielding its
// maximum. 0 when not even one cast fits - the caller delivers first.
inline uint32_t BatchForFreeSlots(uint32_t casts, uint32_t maxYieldPerCast, uint32_t stackSize,
                                  uint32_t freeSlots)
{
    if (casts == 0 || maxYieldPerCast == 0 || stackSize == 0)
        return 0;
    uint64_t room = uint64_t(freeSlots) * stackSize;
    uint64_t batch = room / maxYieldPerCast;
    if (batch > casts)
        batch = casts;
    return uint32_t(batch);
}

// Real free bag slots: backpack slots plus each bag's own free slots.
// Counts slots, not percentages - the 0-100 "bag space" cannot size a batch.
template <typename Bags, typename FreeIn>
inline uint32_t FreeBagSlots(uint32_t backpackUsed, uint32_t backpackSize,
                             Bags const& bags, FreeIn freeIn)
{
    uint32_t free = backpackSize > backpackUsed ? backpackSize - backpackUsed : 0;
    for (auto const& bag : bags)
        free += freeIn(bag);
    return free;
}

} // namespace CraftOrderPolicy
} // namespace TortoiseBots
