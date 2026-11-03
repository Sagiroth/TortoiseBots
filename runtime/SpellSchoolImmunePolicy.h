#pragma once

// A damage spell of a school a creature's template is immune to does nothing
// to it: creature_template.school_immune_mask carries one bit per
// SpellEntry::School (SpellDefines.h: normal 0, holy 1, fire 2, nature 3,
// frost 4, shadow 5, arcane 6), so mask 4 is fire immunity.
//
// The core applies the rule in Creature::IsImmuneToSpell (tortoise-wow
// src/game/Objects/Creature.cpp:2165): a non-positive spell whose school bit
// is in the mask is immune. CastSpellAction::isUseful drops such a cast, so
// the damage rotation skips the row and falls through to a school the target
// takes instead of spending the cast (and the mana) on an immunity.
//
// The numbers are the core's, copied so this stays standalone
// (tools/test_spell_school_immune_policy.cpp).

#include <cstdint>

namespace TortoiseBots {

// schoolMask: the spell's school bit (SpellEntry::GetSpellSchoolMask, i.e.
// 1 << SpellEntry::School). immuneMask: the creature template's
// school_immune_mask; 0 means the template is immune to no school.
inline bool SpellSchoolImmune(std::uint32_t schoolMask, std::uint32_t immuneMask)
{
    return (schoolMask & immuneMask) != 0;
}

} // namespace TortoiseBots
