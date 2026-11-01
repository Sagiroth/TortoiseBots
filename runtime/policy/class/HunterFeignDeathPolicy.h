#pragma once

#include <cstdint>
#include <vector>

// Hunter Feign Death against a mob's only target (owner decision 2026-10-17).
// Pure decision rule with no core types: CastFeignDeathAction::isUseful
// (ai/playerbot/strategy/hunter/HunterActions.cpp) counts, for each mob
// attacking the hunter, the other units on that mob's threat list, and this
// decides (tools/test_hunter_feign_death_policy.cpp).
//
// In 1.12 a mob whose only threat target feigns death has nobody left to
// fight and evades. In a scripted event that resets the encounter: Uldaman
// run 3 (19z12), Keepers attempt 1, the fourth Stone Keeper woke onto the
// hunter, whose "high threat" Feign Death followed 0.9 s later; the keeper
// evaded and EventAI 485701 set the encounter to FAIL. So the hunter does not
// feign while any mob attacking it has only the hunter on its threat list;
// it keeps shooting and the tank picks the mob up. Everywhere else Feign
// Death works as before.

namespace TortoiseBots
{

// `othersPerAttacker`: for each mob whose victim is the hunter, how many
// living units other than the hunter are on its threat list (its pet counts:
// the mob turns to it instead of evading). True when Feign Death may go out.
inline bool HunterFeignDeathAllowed(const std::vector<uint32_t>& othersPerAttacker)
{
    for (uint32_t others : othersPerAttacker)
        if (others == 0)
            return false;
    return true;
}

// Raid groups (warrior-hunter-raid fix 3): medium threat (60 %) on a
// tank-held mob is not a feign. Feign only when a mob is on the hunter
// (non-empty attacker set) or threat is near-max (>= 90 %). Dungeon
// behaviour is the old rule above, unchanged.
inline bool HunterFeignDeathRaidAllowed(bool inRaidGroup, const std::vector<uint32_t>& othersPerAttacker, uint8_t threatPct)
{
    if (!inRaidGroup)
        return HunterFeignDeathAllowed(othersPerAttacker);
    if (!othersPerAttacker.empty())
        return HunterFeignDeathAllowed(othersPerAttacker);
    return threatPct >= 90;
}

} // namespace TortoiseBots
