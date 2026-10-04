#pragma once

#include <cstdint>

// Solo warlock default-pet choice (pool bots only): once the bot knows
// Summon Voidwalker (697, trainer tier 10 in tw_world.spell_template) AND
// holds a Soul Shard (6265, the summon reagent) the autonomous upkeep
// summons the Voidwalker; below that it keeps the Imp. The shard gate keeps
// a shardless bot from queueing a summon that fails isPossible every tick
// (the "no pet" node's summon-imp fallback covers the petless case). The
// rule is a pure function of durable facts and is unit-tested on its own
// (tools/test_warlock_pet_policy.cpp). Owned/hired bots are never touched
// here: the "wrong pet" trigger refuses to fire while a real-player master
// answers for the bot, and the player-chosen strategies stay.

namespace TortoiseBots
{

inline constexpr uint32_t WARLOCK_VOIDWALKER_SUMMON_SPELL = 697;
inline constexpr uint32_t WARLOCK_VOIDWALKER_PET_ENTRY = 1860;
inline constexpr uint32_t WARLOCK_IMP_PET_ENTRY = 416;
inline constexpr uint32_t WARLOCK_SOUL_SHARD_ITEM = 6265;

enum class WarlockSoloPetDecision
{
    LeaveAlone,
    SummonVoidwalker,
};

struct WarlockSoloPetInputs
{
    bool hasRealPlayerMaster = false; // owned/hired bot: player decides
    bool knowsVoidwalker = false;     // HasSpell(697)
    bool hasSoulShard = false;        // holds a 6265 so the summon can fire
    bool hasPet = false;              // any live pet (GetPet / pet target)
    uint32_t currentPetEntry = 0;     // 0 when petless
};

inline WarlockSoloPetDecision DecideWarlockSoloPet(WarlockSoloPetInputs const& inputs)
{
    if (inputs.hasRealPlayerMaster)
        return WarlockSoloPetDecision::LeaveAlone;
    if (!inputs.knowsVoidwalker)
        return WarlockSoloPetDecision::LeaveAlone;
    // Summon Voidwalker consumes a shard; without one the trigger stays
    // quiet (the "no pet" node's summon-imp fallback covers the petless
    // case) so a shardless bot never queues a summon that fails isPossible.
    if (!inputs.hasSoulShard)
        return WarlockSoloPetDecision::LeaveAlone;
    if (inputs.hasPet && inputs.currentPetEntry == WARLOCK_VOIDWALKER_PET_ENTRY)
        return WarlockSoloPetDecision::LeaveAlone;
    if (inputs.hasPet && inputs.currentPetEntry != 0 &&
        inputs.currentPetEntry != WARLOCK_IMP_PET_ENTRY &&
        inputs.currentPetEntry != WARLOCK_VOIDWALKER_PET_ENTRY)
        return WarlockSoloPetDecision::LeaveAlone;
    return WarlockSoloPetDecision::SummonVoidwalker;
}

} // namespace TortoiseBots
