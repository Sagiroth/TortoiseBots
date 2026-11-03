-- Boss tactics: the columns a row can carry beyond its first shape, its primary key's health band, and the Thekal
-- group's own death window.
--
-- hp_min / hp_max: the boss's health % band the row applies in (inclusive). A phase that starts at a health threshold
-- is a band, so one boss can carry the same kind once per band (Skeram's 75/50/25 % splits). The band reads the health
-- of the boss entry the row hangs on, never that of the add the row names: the add's own health is not supported.
-- bot_aura: the row applies only to a bot that carries this aura (Jin'do's Shades take damage only from a bot with
-- Delusions 24306). 0 = every bot.
-- tactic_mode: a per-kind mode word (dispel_priority now|clear|never, flee_on_aura away|to_tank, dps_hold_after and
-- taunt_after_cast engage|blink|until_tank); empty is the kind's own behaviour. `mode` is a MySQL keyword, so the
-- column is `tactic_mode`.
-- window_s: the linked_death release latch's window in seconds (default 7; the load-time value, not a timer on a row).
--
-- The primary key gains the health band, so the same (map, boss, kind, target, spell) can be written once per band.
-- The gate columns are only honoured by a row a fight scan reads; the loader refuses a gate on hold_kill, tank_hold,
-- one_at_a_time and linked_death (they are read straight from the table, never per fight).
--
-- Thekal's group is 15 s, not 7: the row's burn order (Lor'Khan -> Zath -> Thekal) makes every death kill the next
-- corpse's raiser before its 7 s check, so the release has the full 15 s script retry (20261102210000_world.sql).
--
-- Idempotent: each column is added through information_schema.COLUMNS + PREPARE (MySQL 8 and MariaDB), the primary
-- key is rebuilt only while it lacks the band columns, the UPDATE only touches the two Thekal rows, and the DELETE
-- names one exact key. Never edit this file once applied; add a newer migration.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_boss_tactic` ADD COLUMN `hp_min` tinyint unsigned NOT NULL DEFAULT 0 AFTER `burn_order`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_tactic' AND COLUMN_NAME = 'hp_min');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_boss_tactic` ADD COLUMN `hp_max` tinyint unsigned NOT NULL DEFAULT 100 AFTER `hp_min`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_tactic' AND COLUMN_NAME = 'hp_max');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_boss_tactic` ADD COLUMN `bot_aura` int unsigned NOT NULL DEFAULT 0 AFTER `hp_max`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_tactic' AND COLUMN_NAME = 'bot_aura');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_boss_tactic` ADD COLUMN `tactic_mode` varchar(16) NOT NULL DEFAULT '''' AFTER `bot_aura`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_tactic' AND COLUMN_NAME = 'tactic_mode');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_boss_tactic` ADD COLUMN `window_s` float NOT NULL DEFAULT 7 AFTER `tactic_mode`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_tactic' AND COLUMN_NAME = 'window_s');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(DISTINCT `COLUMN_NAME`) = 2,
  'DO 0',
  'ALTER TABLE `ai_boss_tactic` DROP PRIMARY KEY, ADD PRIMARY KEY (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `hp_min`, `hp_max`)')
  FROM information_schema.STATISTICS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_tactic' AND INDEX_NAME = 'PRIMARY'
    AND `COLUMN_NAME` IN ('hp_min', 'hp_max'));
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

UPDATE `ai_boss_tactic` SET `window_s` = 15 WHERE `map` = 309 AND `boss_entry` = 14599 AND `kind` = 'linked_death';

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'interrupt' AND `target_entry` = 0 AND `spell_id` = 24324;
