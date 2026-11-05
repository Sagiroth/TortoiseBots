-- Giver surplus ledger: per-order attribution of base materials the giver
-- supplied (trades and member mail taken in by the order intake). The end
-- of the order mails the unattributed remainder back to the giver before
-- the rest reaches the guild bank. Old rows default to empty.
-- Never edit this file once applied.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `given` varchar(1024) NOT NULL DEFAULT '''' AFTER `bring`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'given');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
