-- Boss tactics: Bloodlord Mandokir 11382's Threatening Gaze 24314 gets a keyed dps_hold_after row, mode until_tank.
--
-- The spell-less until_tank row that 20261103020000_world.sql once carried (and 20261103060000_world.sql deletes) can
-- never recur: a spell-less hold fires only its engage edge. Keyed on the real cast, the hold's cast edge sees each
-- Gaze: Mandokir's cast slot reads 24314 (ReadDpsHold, BossTacticsActions.cpp). A hold lasts param = 4 s from each
-- cast and stays on while the boss's victim is not a tank, up to 3 x param = 12 s (DpsHoldTick, BossTacticsPolicy.h).
--
-- Every id below is from the core (sql/base in tortoise-wow):
--   Spell 24314 Threatening Gaze: tw_world_spell_template.sql:18959. castingTimeIndex (column 18) is 5, a real cast,
--     not the instant 1; interruptFlags (column 21) is 0, so the row is not an interrupt.
--   Mandokir 11382: tw_world_creature_template.sql:118, spell_id4 (column 58) is 24314.
--
-- Target 0: the subject is the boss itself, as for Gri'lek 15082 and Gahz'ranka 15114. Bot aura 0 and no health band.
--
-- Idempotent: the DELETE matches only this one row (map 309, boss 11382, kind dps_hold_after, target 0, spell 24314),
-- so an earlier server that already has it gets the same row back, and no other row of Mandokir (the freeze_on_aura
-- Gaze row or the Ohgan hold_kill row) is touched. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (11382) AND `kind` = 'dps_hold_after' AND `target_entry` = 0 AND `spell_id` = 24314;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `bot_aura`, `tactic_mode`, `comment`) VALUES
(309, 11382, 'dps_hold_after', 0, 24314, 0, 4, 0, 'until_tank', 'Mandokir: Threatening Gaze 24314 picks a random non-top attacker; DPS waits for the tank (4 s, capped at 12 s)');
