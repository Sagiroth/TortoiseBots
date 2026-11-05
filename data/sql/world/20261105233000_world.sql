-- Boss tactics: Jin'do the Hexxer (Zul'Gurub, map 309, creature 11380), the tank's Banish.
--
-- Read from the core (src/scripts/dungeons/zulgurub/boss_jindo.cpp:291-299 and tw_world_spell_target_position.sql):
--   Banish 24466 is cast every 15-35 s on a random player and teleports it to (-11582.9, -1251.15, 90.0), over the
--   ten Sacrificed Trolls 14826 (10 s respawn) 47 yd west of Jin'do's stand. The tank stays his victim, so the
--   spell-less taunt_after_cast until_tank row of 20261105100000_world.sql (spell 0) never opens: it opens on a
--   victim that is no tank, and a Banished tank is still one. He follows it to the pit rim instead and the raid
--   fights there (ZG walk 41, 2026-11-05).
--   This row keys the same tank cover to 24466: a tank the spell moved more than kTauntTeleportYd (20 yd) from him
--   is not holding him, so the first off-tank (priority 1) takes him back inside 10 s (param, mode until_tank). The
--   spell is an instant teleport with no aura and no cast slot, so the victim's distance is the row's trigger. Live
--   only with AiPlayerbot.DungeonRunAuraTactics and AiPlayerbot.DungeonRunCharmYield, as the other taunt rows.
--
-- Idempotent: the DELETE names one exact (map, boss, kind, target, spell, health band) key, and this file's own key
-- is the only one it writes. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'taunt_after_cast' AND `target_entry` = 0 AND `spell_id` = 24466 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `bot_aura`, `tactic_mode`, `comment`) VALUES
(309, 11380, 'taunt_after_cast', 0, 24466, 1, 10, 0, 'until_tank', 'Jindo: Banish 24466 teleports the tank 47 yd onto the pit trolls and he stays Jin''do''s victim; the first off-tank takes him back within 10 s');
