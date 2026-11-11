-- Rollback of data/sql/world/20261108220000_world.sql (Jin'do's threat_successor row opts into `strict`).
--
-- Run this by hand BEFORE starting a TortoiseBots binary from before the `strict` mode word. Such a binary refuses a
-- threat_successor row with any mode word, so on the migrated database it would drop Jin'do's whole successor cap
-- (the damage dealers would be capped at the highest tank again), not only the strict line. This file lives outside
-- data/sql, so the migration runner never applies it.
--
-- It is the inverse UPDATE: tactic_mode back to empty and the comment the row had in 20261106120000. Idempotent.
-- A later start of the new binary does not re-apply 20261108220000 by itself (the runner tracks it as applied); run that
-- file's UPDATE again by hand when moving forward.

UPDATE `ai_boss_tactic` SET `tactic_mode` = '',
  `comment` = 'Jindo: taunt-immune (immunity_flags 6) and Hex wipes the tank threat; the damage dealers keep below the off-tank so it inherits him'
WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'threat_successor' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
