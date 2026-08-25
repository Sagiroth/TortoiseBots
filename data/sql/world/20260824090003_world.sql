-- Remove the obsolete donor RPG cache that was created by the original
-- module migration. No active TortoiseBots source reads or writes this table.
DROP TABLE IF EXISTS `ai_playerbot_rpg_races`;
