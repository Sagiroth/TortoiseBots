-- `ai_dungeon_plan`.`cc_never`: creature entries the automatic crowd control never marks
-- (AiPlayerbot.DungeonRunAutoCc), a comma list as in `cc_priority` ('' none). A trivial
-- NORMAL-rank unit needs no list: its rank excludes it. Read once at startup
-- (ai/playerbot/DungeonPlanMgr.cpp); turtle-agent exports the list later.
--
-- Idempotent per column (information_schema + PREPARE runs on MySQL 8 and MariaDB).
-- Never edit this file once applied; add a newer migration.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_dungeon_plan` ADD COLUMN `cc_never` varchar(255) NOT NULL DEFAULT '''' AFTER `kill_first`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_dungeon_plan' AND COLUMN_NAME = 'cc_never');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
