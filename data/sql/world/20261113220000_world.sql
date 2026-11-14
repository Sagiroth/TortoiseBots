-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834) walks one Son of Hakkar out per Blood
-- Siphon cycle and burns it late (walks 93 + 94, Son-timing variant A).
--
--   seek_pull  priority 3 -> 1, param 75 -> 45 on the row 20261108100000 wrote (priority 3, param 75, window_s 60).
--              priority is the most walk-outs a cycle starts (WalkoutCycleRules::maxPerCycle); 1 means a failed
--              walk-out is not retried that cycle, so at most one Son is pulled per siphon. param is the kill lead:
--              an engaged Son is fought normally and the forced burn starts 45 s before the siphon instead of on
--              arrival, so the seek's 30 % pre-damage hold and the 4 yd gather run, and the Poisonous Blood cloud
--              lands on the gathered raid rather than at the off-tank. window_s stays 60.
--   Walks 93 + 94 (t1106z-r13 image): 50/80 Hakkar kills against 12/112 before the change.
--
-- Rollback: a binary from before this migration reads the row as it stands, so an older binary is not refused any
-- row; it would only walk three Sons out per cycle again. To restore the burn-on-arrival row, run
-- tools/sql/rollback/20261113220000_world.rollback.sql (outside data/sql, so the migration runner never applies it).
--
-- Idempotent: one UPDATE on the one exact seek_pull key; re-running it sets the same values. Never edit this file
-- once applied; add a newer migration.

UPDATE `ai_boss_tactic` SET `priority` = 1, `param` = 45,
  `comment` = 'Hakkar: one Son walk-out per siphon cycle, burn from 45 s (walks 93+94: 50/80 kills vs 12/112)'
WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'seek_pull' AND `target_entry` = 11357 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
