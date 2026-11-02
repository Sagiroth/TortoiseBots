-- Guild order carry: the last bags-plus-mailbox count Tick put on the row.
-- `.bot order list` shows it while the bot is offline, when its bags cannot
-- be read; a live bot is read directly. Old rows default to zero.
-- Never edit this file once applied.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `carried` int unsigned NOT NULL DEFAULT 0 AFTER `reported`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'carried');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
