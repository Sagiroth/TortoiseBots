-- Disenchant service queue: items a guild member mailed for disenchanting.
-- The intake takes them into the bot's bags and records one row per item
-- (which member it goes back to); the service action drains the row once
-- the cast's results are mailed back. Rows survive a restart, so queued
-- items are never silently eaten by the maintenance disenchant: the
-- startup load re-queues them and the service finishes them. A row whose
-- item is no longer in the bags is dropped by the drain.
-- Never edit this file once applied.

CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_disenchant` (
  `bot_guid` int unsigned NOT NULL,
  `sender_guid` int unsigned NOT NULL,
  `item_guid` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  `item_name` varchar(128) NOT NULL DEFAULT '',
  `results` varchar(512) NOT NULL DEFAULT '',
  `pending` tinyint unsigned NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`bot_guid`, `item_guid`)
);
