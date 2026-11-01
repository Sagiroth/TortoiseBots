-- Guild order sub-orders: who each sub-order answers to. `parent_bot` is the
-- order needing the item (0 = top level); `root_giver` is the player who gave
-- the root order, so sub-order live rows reach the right addon. Old rows
-- default to top level: their giver is also their root giver, filled on load
-- below for rows that predate the columns.
-- Never edit this file once applied.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `parent_bot` int unsigned NOT NULL DEFAULT 0 AFTER `giver_guid`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'parent_bot');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `root_giver` int unsigned NOT NULL DEFAULT 0 AFTER `parent_bot`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'root_giver');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

UPDATE `tortoise_bots_guild_order` SET `root_giver` = `giver_guid` WHERE `root_giver` = 0 AND `parent_bot` = 0;
