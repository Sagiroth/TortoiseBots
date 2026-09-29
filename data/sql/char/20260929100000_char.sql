-- Durable hire ledger (issue #192 follow-up): hired companions are temporary.
--
-- One row per character the module itself created through Hire(). It is
-- written exactly once, before the fee is charged, and it is the only proof
-- that a character on a managed pool account may be deleted when its hire
-- ends. A character without a row here is never deleted by the hire system,
-- which is what keeps player characters (and pool characters the module did
-- not create) safe.
--
-- state is 'active' while the companion is hired and 'dismissed' once the hire
-- ended and the character is waiting for its asynchronous deletion. A row that
-- survives a server restart is a stale hire: the module owns no live hire in a
-- fresh process, so every row is recovered and deleted on the first ticks.
--
-- character_account_id is the managed pool account the character lives on;
-- owner_account_id is the player account that paid for the hire. Deletion is
-- revalidated against both the live registry and the character database, so a
-- character that moved accounts is never deleted.
--
-- The row is removed together with the character (core rows by
-- Player::DeleteFromDB, module rows by CharacterCleanup).

CREATE TABLE IF NOT EXISTS `tortoise_bots_hire` (
  `character_guid` INT(10) UNSIGNED NOT NULL,
  `character_account_id` INT(10) UNSIGNED NOT NULL,
  `owner_account_id` INT(10) UNSIGNED NOT NULL,
  `state` VARCHAR(16) NOT NULL DEFAULT 'active',
  `hired_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `dismissed_at` TIMESTAMP NULL DEFAULT NULL,
  PRIMARY KEY (`character_guid`),
  KEY `idx_tortoise_bots_hire_state` (`state`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
