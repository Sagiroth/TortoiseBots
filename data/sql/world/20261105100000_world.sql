-- Boss tactics: Jin'do the Hexxer (Zul'Gurub, map 309, creature 11380), the off-tank cover and the Shade's aura gate.
--
-- Read from the core (src/scripts/dungeons/zulgurub/boss_jindo.cpp, sql/base/tw_world_spell_template.sql):
--   taunt_after_cast  Hex 17172 lands on Jin'do's current victim, the tank, as a magic polymorph and removes the
--                     victim's threat (SpellHitTarget, 131-143); he gives it back as soon as the aura is gone (171-185).
--                     Brain Wash 24261 can charm the tank too. Both leave him standing on a non-tank, so the first
--                     off-tank (priority 1) takes him back inside 10 s (param, mode until_tank). The row is live only
--                     with AiPlayerbot.DungeonRunAuraTactics and AiPlayerbot.DungeonRunCharmYield.
--   needs_aura        A Shade of Jin'do 14986 takes damage only from a player carrying Delusions of Jin'do 24306: its
--                     DamageTaken sets every other hit to 0 (326-330). Nobody without the aura targets it, and the run
--                     guard sends no tank or dps at it (AiPlayerbot.DungeonRunAuraGatedTargets).
-- The boss's other rows (the two dispel_priority rows and the three kill_first rows) stay.
--
-- Idempotent: each DELETE names one exact (map, boss, kind, target, spell, health band) key, and this file's own two
-- keys are the only ones it writes. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'taunt_after_cast' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'needs_aura' AND `target_entry` = 14986 AND `spell_id` = 24306 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `bot_aura`, `tactic_mode`, `comment`) VALUES
(309, 11380, 'taunt_after_cast', 0, 0, 1, 10, 0, 'until_tank', 'Jindo: Hex 17172 wipes the tank threat (boss_jindo.cpp:131-143) and Brain Wash 24261 can charm the tank; the first off-tank takes him back within 10 s'),
(309, 11380, 'needs_aura', 14986, 24306, 0, 0, 0, '', 'Jindo: a Shade 14986 takes damage only from a Delusions 24306 carrier (boss_jindo.cpp:326-330); nobody else targets or taunts it');
