#pragma once

// Quality floor of the fresh-seed band in PlayerbotFactory::InitEquipment
// (issue #1485, F2). The seed shuffles uniformly over every candidate from the
// floor up, and below level 20 the world DB holds far more grey than white
// items (a level-7 bot drew about 65% grey per slot), so a fresh bot at levels
// 5-19 starts at white, not grey. From level 20 the band starts at green.
//
// Grey stays only in the empty-slot backstop (EmptySlotGates::minQuality), which
// exists so a slot is never left bare when the band finds nothing.
//
// Pure and core-free, so tools/test_gear_seed_quality_policy.cpp includes it
// directly. The values mirror the core's ItemQualities; PlayerbotFactory.cpp
// static_asserts that they match.

#include <cstdint>

namespace TortoiseBots
{
inline constexpr std::uint32_t kSeedQualityNormal = 1;    // ITEM_QUALITY_NORMAL (white)
inline constexpr std::uint32_t kSeedQualityUncommon = 2;  // ITEM_QUALITY_UNCOMMON (green)
inline constexpr std::uint32_t kSeedGreenFromLevel = 20;

// Lowest item quality the fresh-seed band may draw for a bot of `botLevel`.
inline constexpr std::uint32_t SeedQualityFloor(std::uint32_t botLevel)
{
    return botLevel < kSeedGreenFromLevel ? kSeedQualityNormal : kSeedQualityUncommon;
}
}
