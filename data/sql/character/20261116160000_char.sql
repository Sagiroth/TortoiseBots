-- AH bot seller account and character registry.
-- Tracks the seller account and its per-faction characters so repairs after
-- account/character deletion find or recreate them, and so pool, hire,
-- alt-bot, cleanup, and dashboard queries can identify seller rows.
CREATE TABLE IF NOT EXISTS `tortoise_bots_ahbot_seller` (
  `guid` INT(10) UNSIGNED NOT NULL,
  `account_id` INT(10) UNSIGNED NOT NULL,
  `faction` ENUM('alliance', 'horde') NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`guid`),
  KEY `idx_tortoise_bots_ahbot_seller_account` (`account_id`),
  KEY `idx_tortoise_bots_ahbot_seller_faction` (`faction`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
