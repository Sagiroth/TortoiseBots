-- Guild orders: per-guild bot rank for guild bank access. `bot_rank` is the
-- rank every bot in the guild is moved to (default 1, the highest rank below
-- the guild master); 0 means the feature is off for that guild and ranks
-- stay as they are. One row per guild, written when the master picks a rank.
-- Never edit this file once applied.

CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_bot_rank` (
  `guild_id` int unsigned NOT NULL,
  `bot_rank` tinyint unsigned NOT NULL DEFAULT 1,
  `updated_at` datetime NOT NULL,
  PRIMARY KEY (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
