-- Interrupt row corrected after the core-data check (pjerra/turtle-agent#246): the core stops a
-- generic cast only when the spell has PreventionType silence (1) and InterruptFlags with the
-- damage bit 0x02 (src/game/Spells/SpellEffects.cpp, Spell::EffectInterruptCast), plus
-- castingTimeIndex > 1. Grimlok's Chain Bolt 8292 (castingTimeIndex 19, preventionType 1) has
-- interruptFlags 0x9, so a kick could never stop it and the row only wasted the kick: removed
-- here, key-scoped. The Lightning Bolt 9532 row (interruptFlags 15) stays.
DELETE FROM `ai_boss_tactic` WHERE `map` = 70 AND `boss_entry` = 4854 AND `kind` = 'interrupt' AND `target_entry` = 0 AND `spell_id` = 8292;
