-- Remove obsolete donor caches that have no active reader or writer in the
-- native module. These tables are module-owned and were never part of bot
-- character state.
DROP TABLE IF EXISTS `ai_playerbot_random_bots`;
DROP TABLE IF EXISTS `ai_playerbot_tele_cache`;
DROP TABLE IF EXISTS `ai_playerbot_rarity_cache`;
