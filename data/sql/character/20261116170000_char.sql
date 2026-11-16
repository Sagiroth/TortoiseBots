-- Lineages: one row per level 1 alt a level 60 pool bot rolled. `parent_guid`
-- is the character that rolled it, `founder_guid` the first character of the
-- line (a character without a row is the founder of its own line). Name,
-- race and class are the ones at creation, so the history survives if the
-- character is removed later. Times are unix seconds; `reached_60_at` is 0
-- until the alt reaches level 60. A pool reset clears the table.
-- Never edit this file once applied.

CREATE TABLE IF NOT EXISTS `tortoise_bots_lineage` (
  `guid` int unsigned NOT NULL,
  `parent_guid` int unsigned NOT NULL,
  `founder_guid` int unsigned NOT NULL,
  `name` varchar(12) NOT NULL DEFAULT '',
  `race` tinyint unsigned NOT NULL DEFAULT 0,
  `class` tinyint unsigned NOT NULL DEFAULT 0,
  `created_at` bigint unsigned NOT NULL DEFAULT 0,
  `reached_60_at` bigint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`),
  KEY `founder_guid` (`founder_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
