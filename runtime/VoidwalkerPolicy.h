#pragma once

#include <cstdint>

// Voidwalker Suffering + Consume Shadows gates (PET-8): Suffering is the
// AoE taunt for a tanking Voidwalker facing multiple mobs — gated on the
// PET-3 taunt permission (never AoE-taunt in a real-tank group) plus a live
// Voidwalker. Consume Shadows is the channeled self-heal, NC-only with a
// hurt demon. Pure decision logic, unit-tested on its own
// (tools/test_voidwalker_policy.cpp); triggers/actions wire live values.

namespace TortoiseBots
{

inline constexpr uint32_t WARLOCK_VOIDWALKER_ENTRY = 1860;

struct SufferingGateInputs
{
    bool hasPet = false;
    uint32_t currentPetEntry = 0;
    bool petTauntAllowed = false; // PET-3 predicate: solo or tankless group
    uint8_t attackerCount = 0;    // mobs on the bot
};

// Suffering is AoE: needs several attackers to justify the cooldown and the
// group-wide attention. Single-target peel stays with Torment.
inline bool CanCastSuffering(SufferingGateInputs const& inputs)
{
    if (!inputs.hasPet || inputs.currentPetEntry != WARLOCK_VOIDWALKER_ENTRY)
        return false;
    if (!inputs.petTauntAllowed)
        return false;
    return inputs.attackerCount >= 3;
}

struct ConsumeShadowsGateInputs
{
    bool hasPet = false;
    uint32_t currentPetEntry = 0;
    bool petAlive = false;
    uint8_t petHealth = 100; // percent
    bool ownerInCombat = false;
    bool mounted = false;
};

inline bool CanCastConsumeShadows(ConsumeShadowsGateInputs const& inputs)
{
    if (!inputs.hasPet || inputs.currentPetEntry != WARLOCK_VOIDWALKER_ENTRY)
        return false;
    if (!inputs.petAlive)
        return false;
    if (inputs.ownerInCombat)
        return false;
    if (inputs.mounted)
        return false;
    return inputs.petHealth < 70;
}

} // namespace TortoiseBots
