-- Native TortoiseBots schema for optional PlayerBots behavior caches.
-- Keep this migration schema-only: servers may load the mature Vanilla/Turtle
-- datasets separately, while an empty cache still leaves manual bots usable.

CREATE TABLE IF NOT EXISTS `ai_playerbot_weightscales` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `name` varchar(32) NOT NULL,
  `class` tinyint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_weightscale_data` (
  `id` int unsigned NOT NULL,
  `field` varchar(32) NOT NULL,
  `val` smallint unsigned NOT NULL,
  KEY `id` (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_enchants` (
  `class` tinyint unsigned NOT NULL,
  `spec` tinyint unsigned NOT NULL,
  `spellid` bigint unsigned NOT NULL,
  `slotid` tinyint unsigned NOT NULL DEFAULT 1,
  `name` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`class`, `spec`, `spellid`, `slotid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_texts` (
  `id` smallint unsigned NOT NULL AUTO_INCREMENT,
  `name` varchar(255) NOT NULL,
  `text` text NOT NULL,
  `say_type` tinyint unsigned NOT NULL DEFAULT 0,
  `reply_type` tinyint unsigned NOT NULL DEFAULT 0,
  `text_loc1` text NOT NULL,
  `text_loc2` text NOT NULL,
  `text_loc3` text NOT NULL,
  `text_loc4` text NOT NULL,
  `text_loc5` text NOT NULL,
  `text_loc6` text NOT NULL,
  `text_loc7` text NOT NULL,
  `text_loc8` text NOT NULL,
  PRIMARY KEY (`id`),
  KEY `name` (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_texts_chance` (
  `name` varchar(255) NOT NULL,
  `probability` tinyint unsigned NOT NULL DEFAULT 100,
  PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_help_texts` (
  `id` smallint unsigned NOT NULL AUTO_INCREMENT,
  `name` varchar(255) NOT NULL,
  `template_text` text NOT NULL,
  `template_changed` tinyint unsigned NOT NULL DEFAULT 0,
  `text` text NOT NULL DEFAULT '',
  `text_loc1` text NOT NULL DEFAULT '',
  `text_loc2` text NOT NULL DEFAULT '',
  `text_loc3` text NOT NULL DEFAULT '',
  `text_loc4` text NOT NULL DEFAULT '',
  `text_loc5` text NOT NULL DEFAULT '',
  `text_loc6` text NOT NULL DEFAULT '',
  `text_loc7` text NOT NULL DEFAULT '',
  `text_loc8` text NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  KEY `name` (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_named_location` (
  `name` varchar(128) NOT NULL,
  `map_id` smallint unsigned NOT NULL,
  `position_x` double NOT NULL,
  `position_y` double NOT NULL,
  `position_z` double NOT NULL,
  `orientation` double NOT NULL,
  `description` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_zone_level` (
  `id` bigint unsigned NOT NULL,
  `level` bigint NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_travelnode` (
  `id` mediumint unsigned NOT NULL,
  `name` varchar(1024) NOT NULL,
  `map_id` mediumint unsigned NOT NULL,
  `x` float NOT NULL,
  `y` float NOT NULL,
  `z` float NOT NULL,
  `linked` tinyint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_travelnode_link` (
  `node_id` mediumint unsigned NOT NULL,
  `to_node_id` mediumint unsigned NOT NULL,
  `type` tinyint unsigned NOT NULL DEFAULT 0,
  `object` bigint unsigned NOT NULL DEFAULT 0,
  `distance` float NOT NULL DEFAULT 0,
  `swim_distance` float NOT NULL DEFAULT 0,
  `extra_cost` float NOT NULL DEFAULT 0,
  `calculated` tinyint unsigned NOT NULL DEFAULT 0,
  `max_creature_0` tinyint unsigned NOT NULL DEFAULT 0,
  `max_creature_1` tinyint unsigned NOT NULL DEFAULT 0,
  `max_creature_2` tinyint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`node_id`, `to_node_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_travelnode_path` (
  `node_id` mediumint unsigned NOT NULL,
  `to_node_id` mediumint unsigned NOT NULL,
  `nr` mediumint unsigned NOT NULL,
  `map_id` mediumint unsigned NOT NULL,
  `x` float NOT NULL,
  `y` float NOT NULL,
  `z` float NOT NULL,
  PRIMARY KEY (`node_id`, `to_node_id`, `nr`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
