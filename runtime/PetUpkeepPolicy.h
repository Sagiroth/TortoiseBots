#pragma once

#include <cstdint>

// Autonomous pet-autocast upkeep (E01, ported from mod-playerbots
// `Ai/Base/Actions/PetsAction.cpp:342` `TogglePetSpellAutoCastAction`).
//
// The sweep enables autocast on every known non-passive pet spell except a
// denylist of situational abilities (Prowl, Cower, Spell Lock, Devour Magic)
// that the bot must trigger deliberately. The list below is the 1.12-existant
// subset of the donor's `disabledPetSpells` (verified against
// `tw_world.spell_template`): donor-only ranks with no 1.12 rows are
// excluded — Devour Magic 27276/27277, master's-call Leap 47482, Spirit Wolf
// Leap 58867, and the 48011 visual. Cower ranks 1753-1756/16697 are not in
// the donor list but are included here to agree with the factory, which
// already toggles every Cower rank off by default
// (`PlayerbotFactory.cpp` `cowerSpellIds`).

namespace TortoiseBots
{

inline constexpr uint32_t kDisabledPetAutocastSpells[] = {
    24450, 24452, 24453,                       // Prowl 1-3
    1742, 1753, 1754, 1755, 1756, 16697,        // Cower 1-6
    19244, 19647,                              // Spell Lock 1-2
    19505, 19731, 19734, 19736,                // Devour Magic 1-4
};

inline bool IsDisabledPetAutocast(uint32_t spellId)
{
    for (uint32_t disabled : kDisabledPetAutocastSpells)
        if (disabled == spellId)
            return true;
    return false;
}

enum class PetAutocastDecision
{
    LeaveAlone,
    Enable,
    Disable,
};

// Pure toggle rule: the desired state is autocast-on unless denylisted.
// The caller (action) resolves known/removed/autocastable via the core Pet
// API; this only decides whether the current state needs to change.
inline PetAutocastDecision DecidePetAutocast(bool currentlyActive, bool disabled)
{
    bool const desired = !disabled;
    if (currentlyActive == desired)
        return PetAutocastDecision::LeaveAlone;
    return desired ? PetAutocastDecision::Enable : PetAutocastDecision::Disable;
}

} // namespace TortoiseBots
