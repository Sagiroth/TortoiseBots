#pragma once

#include <cstddef>
#include <cstdint>

// Class-consumable policy for the c-cons fix (r-poisons findings): pure
// decision rules over durable facts (class, item entry, required level) so
// they stay unit tested on their own
// (tools/test_class_consumable_policy.cpp). Server data, not guesses:
//
// - Soul shards (6265, class REAGENT, stackable 3, sell 0): the pool runs
//   the item cheat, but shards stay REAL - harvested organically with Drain
//   Soul (spell 1120, baseLevel 10; core grants the shard only when the
//   drained target dies while granting XP/honor) and spent on real reagent
//   casts (pet summons, healthstones, soulstones). Keep band is 5, matching
//   the destroy-excess trigger.
// - Poisons (allowable_class 8 = rogue, stack 20, req 20-60): Instant
//   6947/6949/6950/8926/8927/8928/21927, Deadly 2892/2893/8984/8985/20844,
//   Crippling 3775/3776, Mind 5237/6951/9186, Wound 10918/10920/10921/10922.
// - Stones (class TRADE_GOODS 7, stack 20, req 1-50, no class mask):
//   sharpening 2862/2863/2871/7964/12404/18262/23122, weight
//   3239/3240/3241/7965/12643. Upkeep users: warrior, paladin, druid
//   (live "apply stone" queues; hunters excluded by design).
// - Oils (class TRADE_GOODS 7, stackable 1, req 5-45, no class mask):
//   wizard 20744/20746/20750/20749, mana 20745/20747/20748. Upkeep users:
//   paladin, hunter, priest, shaman, mage, warlock, druid (live "apply oil"
//   queues; warrior/rogue never imbue oil).
//
// Class numbers mirror SharedDefines.h. Allowable-class masks are bit
// masks: rogue (class 4) is 1 << 3 = 8.

namespace TortoiseBots
{

inline constexpr uint32_t CLASS_WARRIOR_ID = 1;
inline constexpr uint32_t CLASS_PALADIN_ID = 2;
inline constexpr uint32_t CLASS_HUNTER_ID = 3;
inline constexpr uint32_t CLASS_ROGUE_ID = 4;
inline constexpr uint32_t CLASS_PRIEST_ID = 5;
inline constexpr uint32_t CLASS_SHAMAN_ID = 7;
inline constexpr uint32_t CLASS_MAGE_ID = 8;
inline constexpr uint32_t CLASS_WARLOCK_ID = 9;
inline constexpr uint32_t CLASS_DRUID_ID = 11;

// Soul shard economy: harvest below the keep band, destroy above it.
inline constexpr uint32_t SOUL_SHARD_ITEM_ID = 6265;
inline constexpr uint32_t SOUL_SHARD_KEEP_COUNT = 5;
// Drain Soul execute window: target at/below this health percent.
inline constexpr uint8_t DRAIN_SOUL_TARGET_HEALTH_PCT = 20;

inline bool ShouldDrainSoulForShards(uint32_t shardCount)
{
    return shardCount < SOUL_SHARD_KEEP_COUNT;
}

inline uint32_t SoulShardOverflow(uint32_t shardCount)
{
    return shardCount > SOUL_SHARD_KEEP_COUNT ? shardCount - SOUL_SHARD_KEEP_COUNT : 0;
}

// A class-masked consumable (poisons, class reagents) is worth keeping
// while the bot can use it (req met) and holds no higher tier of the same
// ladder. The old fixed level window (+6) turned the only tier there is
// into vendor stock: Crippling Poison (req 20) is the seed tier up to 49,
// yet it was sold from 27 on and re-seeded on the next ding.
inline bool ShouldKeepClassMaskedConsumable(uint32_t playerClass, uint32_t allowableClass, uint32_t reqLevel, uint32_t botLevel, bool holdsHigherTier)
{
    if (playerClass < 1 || playerClass > 12)
        return false;
    if (allowableClass != (1u << (playerClass - 1)))
        return false;
    if (botLevel < reqLevel)
        return false;
    return !holdsHigherTier;
}

inline constexpr uint32_t SHARPENING_STONE_ENTRIES[] = { 2862, 2863, 2871, 7964, 12404, 18262, 23122 };
inline constexpr uint32_t WEIGHTSTONE_ENTRIES[] = { 3239, 3240, 3241, 7965, 12643 };
inline constexpr uint32_t WIZARD_OIL_ENTRIES[] = { 20744, 20746, 20750, 20749 };
inline constexpr uint32_t MANA_OIL_ENTRIES[] = { 20745, 20747, 20748 };
// Poison ladders, lowest rank first: the seed families of
// PlayerbotFactory::AddConsumables plus Wound Poison I-IV. Ids missing from
// the world DB simply never match.
inline constexpr uint32_t INSTANT_POISON_ENTRIES[] = { 6947, 6949, 6950, 8926, 8927, 8928, 21927 };
inline constexpr uint32_t DEADLY_POISON_ENTRIES[] = { 2892, 2893, 8984, 8985, 20844, 22053, 22054 };
inline constexpr uint32_t CRIPPLING_POISON_ENTRIES[] = { 3775, 3776 };
inline constexpr uint32_t MIND_POISON_ENTRIES[] = { 5237, 6951, 9186 };
inline constexpr uint32_t WOUND_POISON_ENTRIES[] = { 10918, 10920, 10921, 10922 };

// The higher tiers of the ladder an entry sits on (every list above runs
// lowest tier first). Empty for the top tier and for an entry on no ladder.
struct ConsumableTiers
{
    uint32_t const* ids = nullptr;
    std::size_t count = 0;
};

template <std::size_t N>
inline bool FindHigherTiers(uint32_t const (&ladder)[N], uint32_t itemId, ConsumableTiers& out)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        if (ladder[i] == itemId)
        {
            out.ids = ladder + i + 1;
            out.count = N - i - 1;
            return true;
        }
    }
    return false;
}

inline ConsumableTiers HigherTiersOf(uint32_t itemId)
{
    ConsumableTiers out;
    if (FindHigherTiers(SHARPENING_STONE_ENTRIES, itemId, out) || FindHigherTiers(WEIGHTSTONE_ENTRIES, itemId, out) ||
        FindHigherTiers(WIZARD_OIL_ENTRIES, itemId, out) || FindHigherTiers(MANA_OIL_ENTRIES, itemId, out) ||
        FindHigherTiers(INSTANT_POISON_ENTRIES, itemId, out) || FindHigherTiers(DEADLY_POISON_ENTRIES, itemId, out) ||
        FindHigherTiers(CRIPPLING_POISON_ENTRIES, itemId, out) || FindHigherTiers(MIND_POISON_ENTRIES, itemId, out) ||
        FindHigherTiers(WOUND_POISON_ENTRIES, itemId, out))
        return out;
    return ConsumableTiers();
}

// True when the bot holds any higher tier of the entry's ladder. `holds`
// answers "is at least one of this item id in the bags"; it is asked only
// for the handful of higher tiers, never for the whole inventory.
template <typename HoldsItem>
inline bool HoldsHigherTier(uint32_t itemId, HoldsItem holds)
{
    ConsumableTiers higher = HigherTiersOf(itemId);
    for (std::size_t i = 0; i < higher.count; ++i)
        if (holds(higher.ids[i]))
            return true;
    return false;
}

inline bool IsStoneEntry(uint32_t itemId)
{
    for (uint32_t id : SHARPENING_STONE_ENTRIES)
        if (id == itemId)
            return true;
    for (uint32_t id : WEIGHTSTONE_ENTRIES)
        if (id == itemId)
            return true;
    return false;
}

inline bool IsOilEntry(uint32_t itemId)
{
    for (uint32_t id : WIZARD_OIL_ENTRIES)
        if (id == itemId)
            return true;
    for (uint32_t id : MANA_OIL_ENTRIES)
        if (id == itemId)
            return true;
    return false;
}

inline bool UsesStones(uint32_t playerClass)
{
    return playerClass == CLASS_WARRIOR_ID || playerClass == CLASS_PALADIN_ID || playerClass == CLASS_DRUID_ID;
}

inline bool UsesOils(uint32_t playerClass)
{
    return playerClass == CLASS_PALADIN_ID || playerClass == CLASS_HUNTER_ID || playerClass == CLASS_PRIEST_ID ||
           playerClass == CLASS_SHAMAN_ID || playerClass == CLASS_MAGE_ID || playerClass == CLASS_WARLOCK_ID ||
           playerClass == CLASS_DRUID_ID;
}

// Stones/oils carry no class mask, so the class gate lives here. A lower
// tier becomes sellable once the bot holds a higher tier of the same line;
// the old +10 level window sold the seed tier itself (Dense stones, req 35,
// are the seed tier up to 60 but were sold from 46 on).
inline bool ShouldKeepStone(uint32_t playerClass, uint32_t reqLevel, uint32_t botLevel, bool holdsHigherTier)
{
    if (!UsesStones(playerClass))
        return false;
    if (botLevel < reqLevel)
        return false;
    return !holdsHigherTier;
}

inline bool ShouldKeepOil(uint32_t playerClass, uint32_t reqLevel, uint32_t botLevel, bool holdsHigherTier)
{
    if (!UsesOils(playerClass))
        return false;
    if (botLevel < reqLevel)
        return false;
    return !holdsHigherTier;
}

} // namespace TortoiseBots
