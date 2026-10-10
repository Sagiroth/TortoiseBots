#pragma once

#include <cstdint>

// Health Funnel gate (PET-6): the warlock channels own health into the
// demon. Fires only while the pet is badly hurt AND the owner can afford
// the drain — never a suicide channel. Pure decision logic, unit-tested on
// its own (tools/test_health_funnel_policy.cpp); the trigger/action wire it
// to live values.

namespace TortoiseBots
{

struct HealthFunnelGateInputs
{
    bool hasPet = false;
    bool petAlive = false;
    uint8_t petHealth = 100;   // percent
    uint8_t ownerHealth = 100; // percent
    bool ownerInCombat = false;
};

// Pet band: strictly below half. Owner floor: above 60% — the drain is a
// percent game the owner must win. Combat-only v1: out of combat the bot
// eats/bandages and the demon is re-summoned or healed between pulls.
inline bool CanCastHealthFunnel(HealthFunnelGateInputs const& inputs)
{
    if (!inputs.hasPet || !inputs.petAlive)
        return false;
    if (!inputs.ownerInCombat)
        return false;
    if (inputs.petHealth >= 50)
        return false;
    return inputs.ownerHealth > 60;
}

} // namespace TortoiseBots
