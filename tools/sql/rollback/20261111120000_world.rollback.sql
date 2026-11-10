-- Rollback of data/sql/world/20261111120000_world.sql (Hakkar's opening damage hold 5 s to 12 s).
--
-- Run this file by hand to turn r11-2 off. This file lives outside data/sql, so the migration runner never applies
-- it. Run the forward file by hand to turn it on again.
--
-- It deletes the same key and re-inserts the values 20261107120000 wrote (priority 0, param 5, window_s 0).
-- Idempotent.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'dps_hold_after' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'dps_hold_after', 0, 0, 0, 5, 'engage', 0, 0, 0, 0, 0, 0, 'Hakkar: hold DPS 5 s from the first engagement so the main tank holds him before Cause Insanity 24327 picks a damage dealer as its victim');
