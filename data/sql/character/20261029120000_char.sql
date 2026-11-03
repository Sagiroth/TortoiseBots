-- Guild orders: player-chosen farm time and worked totals. `hours` is the whole
-- hours 1-24 given as the trailing argument of `.bot order` (old rows keep 1);
-- `worked_total` is worked seconds over the whole order and ends it as
-- cant/timeout once hours run out. The old moved_zone/deaths counters become
-- zone_index/deaths_in_zone: deaths never end an order, they move the bot to
-- the next zone of its plan.
-- Never edit this file once applied.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `hours` int unsigned NOT NULL DEFAULT 1 AFTER `amount`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'hours');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `worked_total` int unsigned NOT NULL DEFAULT 0 AFTER `worked_sec`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'worked_total');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` CHANGE COLUMN `moved_zone` `zone_index` int unsigned NOT NULL DEFAULT 0',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'zone_index');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` CHANGE COLUMN `deaths` `deaths_in_zone` int unsigned NOT NULL DEFAULT 0',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'deaths_in_zone');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
