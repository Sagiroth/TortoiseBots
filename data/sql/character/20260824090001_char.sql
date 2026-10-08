-- Native TortoiseBots character-side caches and per-bot state.

CREATE TABLE IF NOT EXISTS `ai_playerbot_equip_cache` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `clazz` mediumint NOT NULL,
  `spec` mediumint NOT NULL,
  `lvl` mediumint NOT NULL,
  `slot` mediumint NOT NULL,
  `quality` mediumint NOT NULL,
  `item` mediumint NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_rarity_cache` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `item` mediumint NOT NULL,
  `rarity` float NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_rnditem_cache` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `lvl` mediumint NOT NULL,
  `type` mediumint NOT NULL,
  `item` mediumint NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_tele_cache` (
  `id` mediumint unsigned NOT NULL AUTO_INCREMENT,
  `level` mediumint NOT NULL,
  `map_id` mediumint NOT NULL,
  `x` float NOT NULL,
  `y` float NOT NULL,
  `z` float NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_item_info_cache` (
  `id` bigint unsigned NOT NULL,
  `quality` bigint DEFAULT NULL,
  `slot` bigint DEFAULT NULL,
  `source` mediumint DEFAULT NULL,
  `sourceId` mediumint DEFAULT NULL,
  `team` mediumint DEFAULT NULL,
  `faction` mediumint DEFAULT NULL,
  `factionRepRank` mediumint DEFAULT NULL,
  `minLevel` mediumint DEFAULT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_db_store` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `guid` bigint unsigned NOT NULL,
  `preset` varchar(32) NOT NULL,
  `key` varchar(32) NOT NULL,
  `value` varchar(4000) DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_custom_strategy` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `name` varchar(255) NOT NULL,
  `idx` bigint DEFAULT NULL,
  `owner` bigint DEFAULT NULL,
  `action_line` varchar(1024) NOT NULL,
  PRIMARY KEY (`id`),
  KEY `name` (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_playerbot_random_bots` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `owner` bigint unsigned NOT NULL,
  `bot` bigint unsigned NOT NULL,
  `time` bigint NOT NULL,
  `validIn` bigint DEFAULT NULL,
  `event` varchar(45) DEFAULT NULL,
  `value` bigint DEFAULT NULL,
  `data` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `owner` (`owner`),
  KEY `bot` (`bot`),
  KEY `event` (`event`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
