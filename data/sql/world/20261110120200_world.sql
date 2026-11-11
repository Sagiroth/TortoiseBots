-- Onyxia's Lair (map 249), walk 2b: the tank post becomes hold_spot.
--
-- Slot 1 still posts at the NE spot T (8.0, -200.0, -85.8) for Cindarion 50143 from 65-41 %, but in mode
-- hold_spot (needs AiPlayerbot.DungeonRunTankPostHoldSpot = 1): the post starts even while slot 1 holds
-- another hostile, latches the arrival while Cindarion is alive and selectable, drags him back to T after
-- each breath seat (he follows his tank), and yields to a boss stand seat so the D2 seats move the tank
-- out of the lane. An image without C2 refuses the mode word and drops the row.
--
-- Idempotent: the DELETE removes only the (249, 10184) tank_post row. Never edit this file once applied;
-- add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'tank_post';
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 10184, 'tank_post', 50143, 0, 1, 25, '', 41, 65, 0, 'hold_spot', 120, 0, 0, 8.0, -200.0, -85.8, 'Onyxia 65-41 %: tank slot 1 posts at T for Cindarion, takes him even off a whelp, drags him back to T after each breath seat (needs AiPlayerbot.DungeonRunTankPostHoldSpot)');
