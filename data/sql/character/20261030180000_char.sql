-- Guild orders: keep-in-stock orders. The keep flag plus the stock target
-- (the ordered amount) survive restarts; the last bank look stamps when the
-- keep watch may re-check the bank (a server-uptime ms clock, see the store).
-- Never edit this file once applied.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `keep` tinyint unsigned NOT NULL DEFAULT 0 AFTER `farm_rest`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'keep');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `stock_target` int unsigned NOT NULL DEFAULT 0 AFTER `keep`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'stock_target');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `last_bank_check` int unsigned NOT NULL DEFAULT 0 AFTER `stock_target`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'last_bank_check');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
