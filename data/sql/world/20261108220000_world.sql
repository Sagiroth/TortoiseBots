-- Boss tactics: Jin'do the Hexxer (Zul'Gurub, map 309, creature 11380) opts into the strict successor line, t1106m.
--
--   threat_successor  tactic_mode '' -> 'strict' on Jin'do's row (20261106120000). AiPlayerbot.DungeonRunThreatSuccessorStrict
--                     (no 5 s projection escape, the 92/85 % white-damage stop) now applies only to a boss whose
--                     threat_successor row carries `strict`. It was built for Jin'do's Hex. Walk 57 (t1106l) ran it on
--                     Hakkar too, through his plain row: 15 % of his threat-cap rows had white=held, Hakkar kills fell
--                     from 4/41 (walk 56) to 0/58, and Strict cost about 13 of the 18 points the damage dealers' damage
--                     on him fell.
-- Hakkar's threat_successor row (20261105210000) stays plain: the off-tank cap, no strict line.
--
-- Rollback: a binary from before this change refuses a threat_successor row with a mode word, so on this database it would
-- drop Jin'do's whole successor cap, not only the strict line. Before starting an older binary, run
-- tools/sql/rollback/20261108220000_world.rollback.sql (outside data/sql, so the migration runner never applies it): it
-- puts the row's tactic_mode back to empty and restores its comment.
--
-- Idempotent: one UPDATE on the one exact Jin'do threat_successor key; re-running it sets the same values. Never edit this
-- file once applied; add a newer migration.

UPDATE `ai_boss_tactic` SET `tactic_mode` = 'strict',
  `comment` = 'Jindo: taunt-immune (immunity_flags 6) and Hex wipes the tank threat; the damage dealers keep below the off-tank so it inherits him. strict = AiPlayerbot.DungeonRunThreatSuccessorStrict applies here'
WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'threat_successor' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
