-- Guild relist claim: a row is posted, then claimed (it names the settlement
-- mail and the value that mail promises) before the mail is taken, then
-- received once the value is in the bot's hands. A take whose received write
-- fails is still known from the claim, so the mail is never emptied ahead of
-- the ledger. `received` becomes `state`: 0 posted, 1 claimed, 2 received.
-- Never edit this file once applied.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_relist` ADD COLUMN `state` tinyint unsigned NOT NULL DEFAULT 0 AFTER `expires_at`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_relist' AND COLUMN_NAME = 'state');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_relist` ADD COLUMN `claim_mail` int unsigned NOT NULL DEFAULT 0 AFTER `state`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_relist' AND COLUMN_NAME = 'claim_mail');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 1,
  'UPDATE `tortoise_bots_guild_relist` SET `state` = 2 WHERE `received` = 1',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_relist' AND COLUMN_NAME = 'received');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 1,
  'ALTER TABLE `tortoise_bots_guild_relist` DROP COLUMN `received`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_relist' AND COLUMN_NAME = 'received');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
