-- Guild orders: per-guild bot rank for guild bank access. `bot_rank` is the
-- rank every bot in the guild is moved to (default 1, the highest rank below
-- the guild master); it always holds a real rank (1 to the lowest rank), one
-- row per guild written when the master picks a rank. The on/off switch is
-- `AiPlayerbot.GuildOrderBotRank`, not this table.
-- Never edit this file once applied.

CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_bot_rank` (
  `guild_id` int unsigned NOT NULL,
  `bot_rank` tinyint unsigned NOT NULL DEFAULT 1,
  `updated_at` datetime NOT NULL,
  PRIMARY KEY (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
