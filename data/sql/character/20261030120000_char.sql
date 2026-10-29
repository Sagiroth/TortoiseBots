-- Guild orders: the item actually farmed. For craft orders this is the reagent
-- of the current farm leg (0 = the order item itself); the no-progress clock
-- watches it so honest reagent farming never counts as no progress.
-- Never edit this file once applied.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `farm_item` int unsigned NOT NULL DEFAULT 0 AFTER `farm_chance`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'farm_item');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
