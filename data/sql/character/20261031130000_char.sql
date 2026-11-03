-- Guild order refund stub and column order fix. Never edit this file once applied.

-- The parked refund row from a cancelled order: survives until its guild
-- gold is repaid, then the store clears it.
SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `cancelled` tinyint unsigned NOT NULL DEFAULT 0 AFTER `guild_gold_held`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'cancelled');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- The ladder migration placed `need_items` after `supply_count`; the code
-- reads `supply_gold_count` first, so move the column behind it.
SET @s := (SELECT IF(COUNT(*) > 0,
  'ALTER TABLE `tortoise_bots_guild_order` MODIFY COLUMN `need_items` varchar(1024) NOT NULL DEFAULT '''' AFTER `supply_gold_count`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'need_items');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
