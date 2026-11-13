-- Rollback of data/sql/world/20261113220000_world.sql (Hakkar's seek_pull row: one walk-out a cycle, 45 s kill lead).
--
-- Run this by hand to put the row back to the burn-on-arrival values 20261108100000 wrote (priority 3, param 75,
-- window_s 60, its comment). No older binary refuses the row either way; the values alone change the walk-out.
-- This file lives outside data/sql, so the migration runner never applies it.
--
-- It is the inverse UPDATE: priority and param back, and the comment the row had in 20261108100000. Idempotent.
-- A later start of the new binary does not re-apply 20261113220000 by itself (the runner tracks it as applied);
-- run that file's UPDATE again by hand when moving forward.

UPDATE `ai_boss_tactic` SET `priority` = 3, `param` = 75,
  `comment` = 'Hakkar: up to 3 walk-outs a Blood Siphon cycle, none with less than 60 s left; an engaged Son of Hakkar is burned at once (75 s lead: Poisonous Blood lasts 90 s and the siphon strips it only at its own end)'
WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'seek_pull' AND `target_entry` = 11357 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
