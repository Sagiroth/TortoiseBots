-- Gear source tiers (owner rules, roadmap #289): per-item classification
-- persisted in the item info cache so seed/hire gear rolls filter in O(1).
--
-- Columns on ai_playerbot_item_info_cache:
--   source_tier  TINYINT 0 base / 1 end-game dungeon (229/289/329/800) / 2 raid
--   source_flags TINYINT bitmask: 1 REP (reputation-gated), 2 PVP
--   world_epic   TINYINT 0/1 loot-attested BoE world-drop epic (§6, 62 items)
--
-- Idempotent: re-applying is a no-op once the columns exist. Existing values
-- are kept; new columns default to base/unflagged until
-- AiPlayerbot.GenerateItemCaches rebuilds them on next startup.
-- After apply: restart mangosd with AiPlayerbot.GenerateItemCaches=1 (or run
-- with empty ai_playerbot_item_info_cache) to backfill.

ALTER TABLE `ai_playerbot_item_info_cache`
    ADD COLUMN IF NOT EXISTS `source_tier` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS `source_flags` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS `world_epic` TINYINT UNSIGNED NOT NULL DEFAULT 0;
