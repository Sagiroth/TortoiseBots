-- Rollback of data/sql/world/20261109120000_world.sql (Hakkar's threat_successor row carries Cause Insanity's timer).
--
-- Run this by hand BEFORE starting a TortoiseBots binary from before the reset window. Such a binary refuses a row
-- with `cycle_spell != 0` that is not `seek_aura`, so on the migrated database it would drop Hakkar's whole
-- threat_successor row (the damage dealers would be capped at the highest tank again), not only the timer.
-- This file lives outside data/sql, so the migration runner never applies it.
--
-- It deletes the same key and re-inserts the values 20261105210000 wrote (priority 0, param 0, window_s 0,
-- cycle_spell 0). Idempotent. A later start of the new binary does not re-apply 20261109120000 by itself (the runner
-- tracks it as applied); run that file again by hand when moving forward.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'threat_successor' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'threat_successor', 0, 0, 0, 0, '', 0, 0, 0, 0, 0, 0, 'Hakkar: Cause Insanity 24327 zeroes the main tank and taunts cannot land; the damage dealers keep below the off-tank so a threat reset hands him to it');
