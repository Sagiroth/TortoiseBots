#pragma once

// Quality floor of the fresh-seed band in PlayerbotFactory::InitEquipment
// (issue #1485, F2). The seed shuffles uniformly over every candidate from the
// floor up, and below level 20 the world DB holds far more grey than white
// items (a level-7 bot drew about 65% grey per slot), so a fresh bot at levels
// 5-19 starts at white, not grey. From level 20 the band starts at green.
//
// The empty-slot backstop (EmptySlotGates::minQuality) follows the same floor on
// the fresh-seed path, so a seeded bot never gets grey from it either; a slot
// with only grey candidates stays empty. Non-seed callers of the backstop
// (earned gear, explicit quality, master sync) keep the older grey-below-10 floor.
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

// Lowest quality the empty-slot backstop accepts. `freshSeed` is the seed path
// (PlayerbotFactory's seedSpread); other callers keep grey below level 10.
inline constexpr std::uint32_t BackstopQualityFloor(std::uint32_t botLevel, bool freshSeed)
{
    return freshSeed ? SeedQualityFloor(botLevel)
         : botLevel < 10 ? 0u
         : botLevel < kSeedGreenFromLevel ? kSeedQualityNormal
                                          : kSeedQualityUncommon;
}
}
