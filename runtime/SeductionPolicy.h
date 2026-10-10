#pragma once

#include <cstdint>

// Succubus Seduction gate (PET-2): Seduction only fires while the active
// demon is a Succubus (entry 1863) and the CC mark is a humanoid — the only
// type the core lets Seduction land on. Pure decision logic, unit-tested on
// its own (tools/test_seduction_policy.cpp); the action wires it to live
// values. Range (succubus near the mark) stays in the pet-cast path.

namespace TortoiseBots
{

inline constexpr uint32_t WARLOCK_SUCCUBUS_PET_ENTRY = 1863;

// Mirrors CREATURE_TYPE_HUMANOID (core UnitDefines.h) without pulling core
// headers into the standalone test build.
inline constexpr uint32_t CREATURE_TYPE_HUMANOID_VALUE = 7;

struct SeductionGateInputs
{
    bool hasPet = false;
    uint32_t currentPetEntry = 0;
    bool targetIsPlayer = false;
    uint32_t targetCreatureType = 0;
};

inline bool CanCastSeduction(SeductionGateInputs const& inputs)
{
    if (!inputs.hasPet || inputs.currentPetEntry != WARLOCK_SUCCUBUS_PET_ENTRY)
        return false;
    if (inputs.targetIsPlayer)
        return false;
    return inputs.targetCreatureType == CREATURE_TYPE_HUMANOID_VALUE;
}

} // namespace TortoiseBots
