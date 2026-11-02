-- Guild orders: one row per relisted auction-house surplus the guild paid for.
-- A sale or an expired listing arrives by mail later, often after the order is
-- gone; the ledger keeps that value attached to the guild until the bank gets
-- it. `seq` numbers the rows of one bot; `received` marks a mail already read
-- (the gold or the item is in the bot's hands) with the amounts still to
-- deposit. Never edit this file once applied.

CREATE TABLE IF NOT EXISTS `tortoise_bots_guild_relist` (
  `bot_guid` int unsigned NOT NULL,
  `seq` int unsigned NOT NULL,
  `guild_id` int unsigned NOT NULL,
  `item_id` int unsigned NOT NULL,
  `count` int unsigned NOT NULL,
  `unit_price` int unsigned NOT NULL,
  `posted_at` int unsigned NOT NULL,
  `received` tinyint unsigned NOT NULL DEFAULT 0,
  `held_gold` int unsigned NOT NULL DEFAULT 0,
  `held_count` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`bot_guid`, `seq`),
  KEY `guild_id` (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
