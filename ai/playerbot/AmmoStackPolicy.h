#pragma once

#include <algorithm>
#include <cstdint>

// How many 200-round ammo stacks InitAmmo keeps for a bot. Hunters shoot all
// day and keep the level-scaled amount; warriors and rogues only throw or
// shoot now and then, so they carry a small configurable number (owner
// decision 2026-10-14: 1-2 stacks instead of up to 9). Plain inputs, no core
// includes: tools/test_ammo_stack_policy.cpp exercises it standalone.

namespace ai
{
    // AiPlayerbot.AmmoStacksNonHunter: the code fallback and the value the
    // template shows (the configuration rule: both must be the same).
    constexpr uint32_t kAmmoStacksNonHunterDefault = 2;
    constexpr uint32_t kAmmoStacksNonHunterMax = 10;

    // The hunter amount, the one every bot got before: 5 stacks plus one per
    // ten levels.
    inline uint32_t HunterAmmoStacks(uint32_t level)
    {
        return 5 + level / 10;
    }

    // The configured value as loaded: at least 1, at most kAmmoStacksNonHunterMax.
    inline uint32_t ClampNonHunterAmmoStacks(int32_t configured)
    {
        if (configured < 1)
            return 1;
        return std::min<uint32_t>(static_cast<uint32_t>(configured), kAmmoStacksNonHunterMax);
    }

    // The stack count InitAmmo tops up to. The item cheat (ammo is unlimited
    // for those bots) keeps one stack for everybody. A non-hunter never gets
    // more than a hunter of its level would.
    inline uint32_t AmmoStackTarget(bool hunter, uint32_t level, uint32_t nonHunterStacks, bool itemCheat)
    {
        if (itemCheat)
            return 1;
        uint32_t const hunterStacks = HunterAmmoStacks(level);
        if (hunter)
            return hunterStacks;
        return std::max<uint32_t>(1, std::min(nonHunterStacks, hunterStacks));
    }
}
