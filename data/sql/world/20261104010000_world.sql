-- Boss tactics: the seek_aura row's cycle clock and the spot the source is held at.
--
-- cycle_spell: the spell whose aura on a group member restarts the cycle (Hakkar's Blood Siphon 24324; every group
-- member within 100 yd carries its dummy aura for 8 s).
-- cycle_s: the cycle's length in seconds (Hakkar's siphon timer is 90 s after the first, and every accepted sighting
-- restarts it). The dose window (`window_s`) has to fit inside it.
-- spot_x / spot_y / spot_z: the world point B the source of the cloud is dragged to and killed at (Hakkar: the raid's
-- own stand, -11802.0, -1683.0, 52.9).
-- All five are the seek_aura kind's; the loader refuses them on any other kind (BossTacticsPolicy.h).
--
-- Idempotent: each column is added through information_schema.COLUMNS + PREPARE (MySQL 8 and MariaDB). Never edit
-- this file once applied; add a newer migration.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_boss_tactic` ADD COLUMN `cycle_spell` int unsigned NOT NULL DEFAULT 0 AFTER `window_s`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_tactic' AND COLUMN_NAME = 'cycle_spell');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_boss_tactic` ADD COLUMN `cycle_s` float NOT NULL DEFAULT 0 AFTER `cycle_spell`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_tactic' AND COLUMN_NAME = 'cycle_s');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_boss_tactic` ADD COLUMN `spot_x` float NOT NULL DEFAULT 0 AFTER `cycle_s`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_tactic' AND COLUMN_NAME = 'spot_x');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_boss_tactic` ADD COLUMN `spot_y` float NOT NULL DEFAULT 0 AFTER `spot_x`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_tactic' AND COLUMN_NAME = 'spot_y');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_boss_tactic` ADD COLUMN `spot_z` float NOT NULL DEFAULT 0 AFTER `spot_y`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_tactic' AND COLUMN_NAME = 'spot_z');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
