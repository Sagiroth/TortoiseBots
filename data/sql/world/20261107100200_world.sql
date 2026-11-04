-- Boss tactics: the Gurubashi Berserker's threat-reset cover (Zul'Gurub, map 309, creature 11352).
--
-- Read from the core (src/scripts/dungeons/zulgurub/zulgurub_trash.cpp, GurubashiBerserkerAI):
--   Knock Away 11130 every 10 s on the victim and Intimidating Roar 16508 at 15 s, then every 25-30 s,
--   both call DoResetThreat, so the victim is a player who is not a tank after every reset. The first
--   off-tank (priority 1) taunts the Berserker back within 5 s (param, mode until_tank). The row is live
--   only with AiPlayerbot.DungeonRunAuraTactics and AiPlayerbot.DungeonRunCharmYield.
--   Spell-less on purpose: the resets are script events, not a cast slot or aura the taunt scan keys on.
--
-- Idempotent: the DELETE names the row's own (map, boss, kind, target, spell, health band) key. Never edit
-- this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 11352 AND `kind` = 'taunt_after_cast' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `bot_aura`, `tactic_mode`, `comment`) VALUES
(309, 11352, 'taunt_after_cast', 0, 0, 1, 5, 0, 'until_tank', 'Gurubashi Berserker: Knock Away 11130 every 10 s and Intimidating Roar 16508 every 25-30 s both DoResetThreat (zulgurub_trash.cpp GurubashiBerserkerAI); the first off-tank takes it back within 5 s');
