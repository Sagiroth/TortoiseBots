#pragma once

#include <cstdint>

// Solo warlock default-pet choice (pool bots only): once the bot knows
// Summon Voidwalker (697, trainer tier 10 in tw_world.spell_template) the
// autonomous upkeep summons the Voidwalker; below that it keeps the Imp.
// The rule is a pure function of durable facts and is unit-tested on its
// own (tools/test_warlock_pet_policy.cpp). Owned/hired bots are never
// touched here: the "wrong pet" trigger refuses to fire while a real-player
// master answers for the bot, and the player-chosen strategies stay.

namespace TortoiseBots
{

inline constexpr uint32_t WARLOCK_VOIDWALKER_SUMMON_SPELL = 697;
inline constexpr uint32_t WARLOCK_VOIDWALKER_PET_ENTRY = 1860;
inline constexpr uint32_t WARLOCK_IMP_PET_ENTRY = 416;

enum class WarlockSoloPetDecision
{
    LeaveAlone,
    SummonVoidwalker,
};

struct WarlockSoloPetInputs
{
    bool hasRealPlayerMaster = false; // owned/hired bot: player decides
    bool knowsVoidwalker = false;     // HasSpell(697)
    bool hasPet = false;              // any live pet (GetPet / pet target)
    uint32_t currentPetEntry = 0;     // 0 when petless
};

inline WarlockSoloPetDecision DecideWarlockSoloPet(WarlockSoloPetInputs const& inputs)
{
    if (inputs.hasRealPlayerMaster)
        return WarlockSoloPetDecision::LeaveAlone;
    if (!inputs.knowsVoidwalker)
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
