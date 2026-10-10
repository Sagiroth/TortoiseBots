#pragma once

#include <cstdint>

// Combat pet recovery gate (PET-5): Revive Pet is a 10s channel, so the
// combat node only fires with nobody hitting the bot. Pure decision logic,
// unit-tested on its own (tools/test_pet_revive_policy.cpp); the trigger
// wires it to live values.

namespace TortoiseBots
{

struct PetReviveGateInputs
{
    bool petDead = false;
    uint8_t attackerCount = 0;
    bool mounted = false;
};

inline bool CanRevivePetNow(PetReviveGateInputs const& inputs)
{
    if (!inputs.petDead)
        return false;
    if (inputs.attackerCount > 0)
        return false;
    return !inputs.mounted;
}

} // namespace TortoiseBots
