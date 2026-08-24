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

CREATE TABLE IF NOT EXISTS `ai_playerbot_rnditem_cache` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `lvl` mediumint NOT NULL,
  `type` mediumint NOT NULL,
  `item` mediumint NOT NULL,
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
  `scale_1` mediumint NOT NULL DEFAULT 0,
  `scale_2` mediumint NOT NULL DEFAULT 0,
  `scale_3` mediumint NOT NULL DEFAULT 0,
  `scale_4` mediumint NOT NULL DEFAULT 0,
  `scale_5` mediumint NOT NULL DEFAULT 0,
  `scale_6` mediumint NOT NULL DEFAULT 0,
  `scale_7` mediumint NOT NULL DEFAULT 0,
  `scale_8` mediumint NOT NULL DEFAULT 0,
  `scale_9` mediumint NOT NULL DEFAULT 0,
  `scale_10` mediumint NOT NULL DEFAULT 0,
  `scale_11` mediumint NOT NULL DEFAULT 0,
  `scale_12` mediumint NOT NULL DEFAULT 0,
  `scale_13` mediumint NOT NULL DEFAULT 0,
  `scale_14` mediumint NOT NULL DEFAULT 0,
  `scale_15` mediumint NOT NULL DEFAULT 0,
  `scale_16` mediumint NOT NULL DEFAULT 0,
  `scale_17` mediumint NOT NULL DEFAULT 0,
  `scale_18` mediumint NOT NULL DEFAULT 0,
  `scale_19` mediumint NOT NULL DEFAULT 0,
  `scale_20` mediumint NOT NULL DEFAULT 0,
  `scale_21` mediumint NOT NULL DEFAULT 0,
  `scale_22` mediumint NOT NULL DEFAULT 0,
  `scale_23` mediumint NOT NULL DEFAULT 0,
  `scale_24` mediumint NOT NULL DEFAULT 0,
  `scale_25` mediumint NOT NULL DEFAULT 0,
  `scale_26` mediumint NOT NULL DEFAULT 0,
  `scale_27` mediumint NOT NULL DEFAULT 0,
  `scale_28` mediumint NOT NULL DEFAULT 0,
  `scale_29` mediumint NOT NULL DEFAULT 0,
  `scale_30` mediumint NOT NULL DEFAULT 0,
  `scale_31` mediumint NOT NULL DEFAULT 0,
  `scale_32` mediumint NOT NULL DEFAULT 0,
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
