-- Guild orders: buy orders whose auction attempt is over farm what is still
-- missing. The farm_rest flag survives restarts; the order type stays "buy".
-- Never edit this file once applied.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `farm_rest` tinyint unsigned NOT NULL DEFAULT 0 AFTER `reported`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'farm_rest');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
