#pragma once

#include "GuildOrderContract.h"

#include <cstdint>

// Pure rules for guild fishing orders: which fishing pole the plan needs and
// whether the bot's skill (plus pole and lure) reaches a zone. Header-only
// and free of core types so the rules stay unit tested on their own
// (tools/test_guild_order_fishing_policy.cpp); ai/playerbot/GuildOrderSources.h
// feeds them plain values read from core lookup calls and the world DB.
namespace TortoiseBots
{
namespace GuildOrders
{

// Fishing poles worth granting, worst first: Strong (+5, item 6365) up to the
// best classic pole (Arcanite, item 19970, +35). The plan picks the cheapest
// pole that still reaches the zone; the bot is granted that pole, so owning
// a worse one never blocks an order the grant can fix.
struct FishingPoleBonus
{
    uint32_t itemId = 0;
    uint32_t bonus = 0;
};

inline FishingPoleBonus const* FishingPoles(size_t& count)
{
    static FishingPoleBonus const poles[] = {
        { 6365, 5 },    // Strong Fishing Pole +5
        { 6366, 15 },   // Darkwood Fishing Pole +15
        { 6367, 20 },   // Big Iron Fishing Pole +20
        { 19022, 25 },  // Nat Pagle's Extreme Angler FC-5000 +25
        { 19970, 35 },  // Arcanite Fishing Pole +35
    };
    count = sizeof(poles) / sizeof(poles[0]);
    return poles;
}

// Best fishing-pole bonus the grant can provide.
inline uint32_t BestFishingPoleBonus()
{
    size_t count = 0;
    FishingPoleBonus const* poles = FishingPoles(count);
    uint32_t best = 0;
    for (size_t i = 0; i < count; ++i)
        best = poles[i].bonus > best ? poles[i].bonus : best;
    return best;
}

// Best lure the grant can provide (Aquadynamic Fish Attractor, +100).
// Lures are consumed on use, so the bot carries the item and reapplies it
// while fishing; the planner counts the bonus, like the equipped pole.
inline constexpr uint32_t kBestFishingLureBonus = 100;
inline constexpr uint32_t kBestFishingLureItemId = 6533;  // Aquadynamic Fish Attractor +100
inline constexpr uint32_t kFishingLureGrantCount = 20;

// A fishing zone counts when the bot's own skill plus the best granted
// pole and lure reaches the zone's requirement. Non-positive requirement:
// the live table centers cities at -20 and quest zones at -70, so those
// always count, like the travel fish gate does.
inline bool FishingZoneReachable(int32_t reqSkill, uint32_t skillValue)
{
    if (reqSkill <= 0)
        return true;
    return skillValue + BestFishingPoleBonus() + kBestFishingLureBonus >= uint32_t(reqSkill);
}

// Cheapest pole that reaches the zone with the bot's own skill plus the
// best lure, 0 when even the best pole is not enough (or none is needed).
inline uint32_t FishingPoleFor(int32_t reqSkill, uint32_t skillValue)
{
    if (reqSkill <= 0)
        return 0;
    uint32_t const withLure = skillValue + kBestFishingLureBonus;
    if (withLure >= uint32_t(reqSkill))
        return 0;
    size_t count = 0;
    FishingPoleBonus const* poles = FishingPoles(count);
    for (size_t i = 0; i < count; ++i)
        if (withLure + poles[i].bonus >= uint32_t(reqSkill))
            return poles[i].itemId;
    return 0;
}

// Whether the fishing grant changes anything: the bot reaches zones it
// could not reach on its own skill alone.
inline bool FishingGearHelps(uint32_t skillValue, int32_t reqSkill)
{
    return reqSkill > 0 && skillValue < uint32_t(reqSkill) &&
           FishingZoneReachable(reqSkill, skillValue);
}

// Verdict flags for one fish row in the plan loop: any fish row makes the
// item sourced, and one on the bot's continent makes it ranged, so a
// fish-only item out of skill answers lowskill/noskill instead of nosource.
inline void FishSourceWitness(bool& sawAny, bool& sawInRange, bool onBotContinent)
{
    sawAny = true;
    if (onBotContinent)
        sawInRange = true;
}

} // namespace GuildOrders
} // namespace TortoiseBots
