-- Rebuild the seed gear cache after the gear weight scale review
-- (world migration 20261105150000). Which items a spec may roll is decided
-- when the cache is built, so items that scored 0 before (pure +healing for
-- discipline, Nature damage for balance) never enter it until it is rebuilt.
-- Empty is safe: with AiPlayerbot.GenerateItemCaches on (the default) the
-- next startup regenerates it. Never edit this file once applied.

DELETE FROM `ai_playerbot_equip_cache`;
