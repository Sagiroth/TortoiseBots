-- Guild relist identity: the listing terms the auction house reports back in
-- its notification, so a mail settles the auction that posted it and never an
-- unrelated auction of the same item. `expires_at` is the posting plus the
-- listing duration; an expired notification only settles a finished listing.
-- Rows written before this migration have no terms and settle only by the
-- stale sweep. Never edit this file once applied.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_relist` ADD COLUMN `bid` int unsigned NOT NULL DEFAULT 0 AFTER `unit_price`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_relist' AND COLUMN_NAME = 'bid');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_relist` ADD COLUMN `buyout` int unsigned NOT NULL DEFAULT 0 AFTER `bid`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_relist' AND COLUMN_NAME = 'buyout');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_relist` ADD COLUMN `deposit` int unsigned NOT NULL DEFAULT 0 AFTER `buyout`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_relist' AND COLUMN_NAME = 'deposit');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_relist` ADD COLUMN `expires_at` int unsigned NOT NULL DEFAULT 0 AFTER `posted_at`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_relist' AND COLUMN_NAME = 'expires_at');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
