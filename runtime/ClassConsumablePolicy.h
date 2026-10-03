#pragma once

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
// while the bot is inside its level window: usable now (req met) and not
// yet two tiers stale (+6, mirrors the poison rank spacing).
inline bool ShouldKeepClassMaskedConsumable(uint32_t playerClass, uint32_t allowableClass, uint32_t reqLevel, uint32_t botLevel)
{
    if (playerClass < 1 || playerClass > 12)
        return false;
    if (allowableClass != (1u << (playerClass - 1)))
        return false;
    if (botLevel < reqLevel)
        return false;
    return botLevel <= reqLevel + 6;
}

inline constexpr uint32_t SHARPENING_STONE_ENTRIES[] = { 2862, 2863, 2871, 7964, 12404, 18262, 23122 };
inline constexpr uint32_t WEIGHTSTONE_ENTRIES[] = { 3239, 3240, 3241, 7965, 12643 };
inline constexpr uint32_t WIZARD_OIL_ENTRIES[] = { 20744, 20746, 20750, 20749 };
inline constexpr uint32_t MANA_OIL_ENTRIES[] = { 20745, 20747, 20748 };

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

// Stones/oils carry no class mask, so the class gate lives here. Window
// is +10 (stone/oil tiers sit ~10 levels apart); stale lower tiers fall
// out and become sellable again.
inline bool ShouldKeepStone(uint32_t playerClass, uint32_t reqLevel, uint32_t botLevel)
{
    if (!UsesStones(playerClass))
        return false;
    if (botLevel < reqLevel)
        return false;
    return botLevel <= reqLevel + 10;
}

inline bool ShouldKeepOil(uint32_t playerClass, uint32_t reqLevel, uint32_t botLevel)
{
    if (!UsesOils(playerClass))
        return false;
    if (botLevel < reqLevel)
        return false;
    return botLevel <= reqLevel + 10;
}

} // namespace TortoiseBots
