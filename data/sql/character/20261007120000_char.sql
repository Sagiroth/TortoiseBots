-- Durable claimed-bot ledger (Issue #489): wandering bots claimed into player guilds
--
-- One row per wandering world bot that has been invited into a player guild.
-- Claimed bots remain on their pool account (avoiding the 10-character player account cap)
-- but are owned by their claiming player for gear management, raid roster assignment,
-- and companion control.
--
-- Invariants:
-- 1. A bot can be claimed by at most one player / guild at a time (bot_guid PRIMARY KEY).
-- 2. Claimed bots are permanently exempt from random bot pool resets and re-randomization.
-- 3. When a bot is removed from the guild (or deleted), its claim record is deleted.

CREATE TABLE IF NOT EXISTS `tortoise_bots_claimed` (
  `bot_guid` INT(10) UNSIGNED NOT NULL,
  `owner_account_id` INT(10) UNSIGNED NOT NULL,
  `owner_player_guid` INT(10) UNSIGNED NOT NULL,
  `guild_id` INT(10) UNSIGNED NOT NULL,
  `claimed_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`bot_guid`),
  KEY `idx_tortoise_bots_claimed_owner` (`owner_player_guid`),
  KEY `idx_tortoise_bots_claimed_guild` (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
