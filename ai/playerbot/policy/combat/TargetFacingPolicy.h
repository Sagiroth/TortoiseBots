#pragma once

#include <cstdint>

namespace ai
{
    // The spell attributes the core reads as "the target must be facing you" (Gouge 11285 and its ranks:
    // Attributes = 1376272). Spell::CheckCast compares the whole field with ==, not a bit test, so a spell with one
    // more attribute bit (0x150011) is not refused.
    constexpr std::uint32_t kTargetMustFaceCasterAttributes = 0x150010;

    // Whether the core refuses a cast on a unit target because of where the target faces, a refusal no turn of the
    // caster can fix. Mirrors Spell::CheckCast (src/game/Spells/Spell.cpp, "Must be behind the target" and "Target must
    // be facing you"):
    //   from-behind-only (SpellEntry::IsFromBehindOnlySpell: Backstab, Ambush, Garrote...) and the target has the
    //   caster in its front arc -> SPELL_FAILED_NOT_BEHIND;
    //   Attributes == 0x150010 (Gouge) and the target does not have the caster in its front arc ->
    //   SPELL_FAILED_NOT_INFRONT.
    // `targetFacesCaster` is the core's own test, target->HasInArc(caster) with the default arc. Both refusals call
    // SendInterrupted() from inside the check, so probing such a cast with CheckCast broadcasts a spell failure to every
    // client nearby; PlayerbotAI::CanCastSpell asks this first (AiPlayerbot.SpellFacingPrecheck) and refuses without
    // the probe. Tested in tools/test_target_facing_policy.cpp.
    inline bool TargetFacingRefuses(std::uint32_t attributes, bool fromBehindOnly, bool targetFacesCaster)
    {
        return (fromBehindOnly && targetFacesCaster) ||
               (attributes == kTargetMustFaceCasterAttributes && !targetFacesCaster);
    }
}
