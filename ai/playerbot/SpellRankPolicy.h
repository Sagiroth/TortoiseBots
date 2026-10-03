#pragma once

// Pure decision rule for issue #381: a bot may only be taught a spell rank it
// could have earned at its own level. The taught spell's own level comes from
// the live data (spell_template.spellLevel), not from the trainer row: several
// trainer rows carry a lower reqLevel than the spell they teach (Crusader
// Strike 1: req 8, spell 10; Intimidating Shout: req 20, spell 22;
// Cure Disease: req 14, spell 22; the shaman row that teaches Elemental Fury
// 877: req 6, spell 35), and the downgrade path (hire at a level below the
// character's own) never re-runs the trainer gate at all.
//
// The level alone is not the whole gate: talent-only abilities share a
// teaching spell with the trainer path (Elemental Fury 877 teaches from
// shaman-trainer row 1229 to any warrior whose trainer_class matches, with no
// class-mask row to reject it), so a non-talent spell with no learnable row
// for the bot's level stays untaught even when the trainer would show it.
//
// Both callers (PlayerbotFactory::InitClassLevelSpells for seed/hire, and the
// paid TrainerAction visit) share this rule; the world-facing part (trainer
// state, talent ownership) lives at the call site.

#include <cstdint>

namespace ai
{

// A spell rank the bot may be taught right now: the taught spell itself is at
// or below the bot's level, and the trainer row that teaches it is not a
// higher-rank row being accepted early (higher ranks of the same chain teach
// the same shape through a red row the core already rejects).
inline bool SpellRankTeachableNow(uint32_t botLevel, uint32_t taughtSpellLevel,
    bool taughtSpellHasLevel, bool trainerRowUsable)
{
    if (!trainerRowUsable)
        return false;
    // Spells with no level in the data (racials, mounts, form placeholders)
    // carry no rank gate; the trainer state is the only filter.
    if (!taughtSpellHasLevel)
        return true;
    return botLevel >= taughtSpellLevel;
}

// Cleanup rule for the hire downgrade path: a spell whose own level is above
// the bot's level is never legitimate on a levelled character, whatever
// taught it (trainer gate skipped on the way down, talent rows, quest
// leftovers). Spells with no level in the data are never pruned here.
inline bool SpellOverLevelForBot(uint32_t botLevel, uint32_t spellLevel, bool spellHasLevel)
{
    return spellHasLevel && spellLevel > botLevel;
}

} // namespace ai
