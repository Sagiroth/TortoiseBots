-- Guild orders: progress watchdog column. `worked_nogain` is worked seconds
-- since the bot last gained an order item (or craft-leg reagent); any new
-- item resets it, a zone move does not. Past 3 worked hours the order ends
-- cant/noprogress after delivering what it holds, capped or not.
-- Old rows start at 0: the watchdog only counts from here on.
-- Never edit this file once applied.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `worked_nogain` int unsigned NOT NULL DEFAULT 0 AFTER `worked_total`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'worked_nogain');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
