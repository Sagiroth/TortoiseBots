#pragma once

#include <cstdint>

// Felhunter Devour Magic gate (PET-1, parity with mod-playerbots
// `GenericWarlockStrategy.cpp:63-78` devour purge/cleanse nodes): the
// purge and cleanse actions may only fire while the active demon is a
// Felhunter (entry 417). The C++ action classes hold the same gate inline;
// this policy keeps the rule unit-tested on its own without a core build.

namespace TortoiseBots
{

inline constexpr uint32_t WARLOCK_FELHUNTER_PET_ENTRY = 417;

struct DevourMagicGateInputs
{
    bool hasPet = false;
    uint32_t currentPetEntry = 0;
};

inline bool CanCastDevourMagic(DevourMagicGateInputs const& inputs)
{
    return inputs.hasPet && inputs.currentPetEntry == WARLOCK_FELHUNTER_PET_ENTRY;
}

} // namespace TortoiseBots
