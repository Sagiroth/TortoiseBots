-- Durable manual PlayerBots ownership. Runtime Headless records are
-- intentionally transient; this table is the authoritative relogin/roster
-- source and keeps ownership when .bot logout/remove stops a runtime.
CREATE TABLE IF NOT EXISTS `tortoise_bots_owned_character` (
  `character_guid` INT(10) UNSIGNED NOT NULL,
  `owner_account_id` INT(10) UNSIGNED NOT NULL,
  `character_account_id` INT(10) UNSIGNED NOT NULL,
  `master_guid` INT(10) UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`character_guid`),
  KEY `idx_tortoise_bots_owner` (`owner_account_id`),
  KEY `idx_tortoise_bots_character_account` (`character_account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
