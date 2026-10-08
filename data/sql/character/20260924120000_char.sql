-- Managed random-bot pool: the authoritative account registry and the reset
-- generation state (issue #265).
--
-- tortoise_bots_pool_account lists the login-account IDs this module owns as
-- random-pool infrastructure. It is authoritative for every random-pool
-- operation (discovery, hire reuse, auto-create, reset target selection).
-- AiPlayerbot.RandomBotAccountPrefix never authorizes deletion; an account
-- that merely looks like a bot account stays untouched until an administrator
-- enrolls it with `bot pool adopt confirm`.
--
-- registration_source is audit information only ('auto-create', 'hire',
-- 'adopted'); no code branches on it.
--
-- No foreign keys to the login database: account existence and username
-- equality are validated in code whenever the registry is loaded for a
-- destructive operation.

CREATE TABLE IF NOT EXISTS `tortoise_bots_pool_account` (
  `account_id` INT(10) UNSIGNED NOT NULL,
  `username_at_registration` VARCHAR(32) NOT NULL,
  `registration_source` VARCHAR(16) NOT NULL,
  `registered_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`),
  UNIQUE KEY `uq_tortoise_bots_pool_username` (`username_at_registration`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- One row per managed pool. `applied_generation` holds the last one-shot
-- token that completed deletion and verification; it is written only after a
-- successful reset, so a crash mid-reset resumes on the next startup instead
-- of being recorded as applied. `completed_at` is diagnostics for both
-- one-shot and development `always` resets.
CREATE TABLE IF NOT EXISTS `tortoise_bots_pool_state` (
  `pool_name` VARCHAR(32) NOT NULL,
  `applied_generation` VARCHAR(128) NOT NULL DEFAULT '',
  `completed_at` TIMESTAMP NULL DEFAULT NULL,
  PRIMARY KEY (`pool_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
