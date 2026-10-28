-- Guild orders given with `.bot order` (one row per bot). The store loads this
-- table once at startup and keeps it in memory; the AI reads memory, never the
-- database per tick. A new order replaces the old one (REPLACE INTO on the
-- bot_guid primary key). Finished orders stay visible until the player cancels
-- them or gives a new one. The character-deletion cleanup removes the row with
-- the character; leaving the guild cancels the order.
-- Never edit this file once applied.

CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_order` (
  `bot_guid` int unsigned NOT NULL,
  `guild_id` int unsigned NOT NULL,
  `giver_guid` int unsigned NOT NULL,
  `type` varchar(8) NOT NULL DEFAULT 'farm',
  `item_id` int unsigned NOT NULL,
  `amount` int unsigned NOT NULL,
  `delivered` int unsigned NOT NULL DEFAULT 0,
  `state` varchar(12) NOT NULL DEFAULT 'working',
  `reason` varchar(12) NOT NULL DEFAULT '-',
  `farm_zone` int unsigned NOT NULL DEFAULT 0,
  `farm_chance` tinyint unsigned NOT NULL DEFAULT 0,
  `moved_zone` tinyint unsigned NOT NULL DEFAULT 0,
  `deaths` int unsigned NOT NULL DEFAULT 0,
  `worked_sec` int unsigned NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL,
  `updated_at` datetime NOT NULL,
  PRIMARY KEY (`bot_guid`),
  KEY `idx_guild` (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
