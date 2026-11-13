-- Onyxia's Lair (map 249), walk 2e: the add crew takes the whelps once Cindarion is dead.
--
-- Walk 2d a-1: at least 79 % of the P2 damage landed on the whelps, the ranged bots stood on a whelp for 37 % of
-- their frames, and she took 0.65 %/min, so the whelps stay with a crew: melee, tanks and the first two AoE ranged
-- bots; the other ranged bots go back to her while she hovers (needs AiPlayerbot.DungeonRunAddCrew = 1).
--
-- Idempotent: the DELETE removes only the (249, 10184) add_crew row. Never edit this file once applied;
-- add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'add_crew';
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 10184, 'add_crew', 50143, 0, 2, 0, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0, 'Onyxia: once Cindarion is dead, melee, tanks and 2 AoE ranged take the whelps and the other ranged stay on her (needs AiPlayerbot.DungeonRunAddCrew); walk 2d a-1: >= 79 % of P2 damage on whelps, 0.65 %/min on her');
