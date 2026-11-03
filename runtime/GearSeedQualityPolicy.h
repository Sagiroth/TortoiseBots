#pragma once

// Quality floor of the fresh-seed band in PlayerbotFactory::InitEquipment
// (issue #1485, F2). The seed shuffles uniformly over every candidate from the
// floor up, and below level 20 the world DB holds far more grey than white
// items (a level-7 bot drew about 65% grey per slot), so a fresh bot at levels
// 5-19 starts at white, not grey. From level 20 the band starts at green.
//
// The empty-slot backstop (EmptySlotGates::minQuality) follows the same floor on
// the fresh pool-bot seed, so a seeded bot never gets grey from it either; a slot
// with only grey candidates stays empty. Only that seed gets the white floor.
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

// Lowest quality the seed band and the empty-slot backstop accept. `freshPoolSeed`
// is true only on the one-time pool-bot seed (PlayerbotFactory::MakeComplete(true));
// every other caller (player autogear, hired-companion provisioning, re-gearing,
// owned-bot autogear) keeps the older floor: grey below 10, white 10-19, green from 20.
inline constexpr std::uint32_t GearQualityFloor(std::uint32_t botLevel, bool freshPoolSeed)
{
    return freshPoolSeed ? SeedQualityFloor(botLevel)
         : botLevel < 10 ? 0u
         : botLevel < kSeedGreenFromLevel ? kSeedQualityNormal
                                          : kSeedQualityUncommon;
}
}
