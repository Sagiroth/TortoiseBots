-- Guild orders: the "materials I bring" tick set (`bring=` on craft orders
-- and craft previews). The bot never sources a ticked base reagent; the rest
-- of it lands on the giver's need list. Stored as comma-separated item ids
-- (`-` on the wire when empty). Old rows default to empty.
-- Never edit this file once applied.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `tortoise_bots_guild_order` ADD COLUMN `bring` varchar(1024) NOT NULL DEFAULT '''' AFTER `carried`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'tortoise_bots_guild_order' AND COLUMN_NAME = 'bring');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
