-- Onyxia's Lair (map 249), walk 2a: the tank post for Cindarion 50143, the DPS gate on Onyxia 10184,
-- and the room-centre anchor that keeps the raid out of the respawned room-mouth Warder.
--
-- tank_post: slot 1 waits at the NE spot T (8.0, -200.0, -85.8) from 65 % to 41 % and attacks Cindarion the moment
-- he is selectable within 25 yd of it. His touchdown (-8.9, -212.8, -88.5, walk 1, 5 of 5 pulls) is 21.4 yd away;
-- the path from his landing to T is clear (turtle-agent path-check complete, 21.2 yd). The band starts at the
-- take-off so the tank is already posted when he lands; window 120 s (the longest the kind allows; the post
-- restarts at once after an expiry, harmless on today's code). Needs AiPlayerbot.DungeonRunTankPost = 1.
--
-- gate_hold: no damage on Onyxia at or below 42 % while Cindarion fights (in combat, not evading) within the
-- default 200 yd radius. If she dies first he evades and is unkillable for the lockout. Needs
-- AiPlayerbot.DungeonRunGateHold = 1.
--
-- anchor: every boss/raid move stays within 60 yd of the room centre (-22.0, -213.5, -86.5), so the west edge is
-- x -82: the respawned room-mouth Warder (-113.6, -216.8, aggro 20 yd) is never chased into (walk 1: 11 of 40
-- listed deaths). The centre z keeps every room floor point inside the kind's 4 yd height rule. The raid approach
-- stand (-80, -211.4) is inside (58 yd).
--
-- Idempotent: each DELETE names only this file's rows. The existing 10184 air_target row (none: walk 1 wrote no
-- Onyxia row) and the 50143/50144 rows are untouched. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` IN ('tank_post', 'gate_hold', 'anchor');
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 10184, 'tank_post', 50143, 0, 1, 25, '', 41, 65, 0, '', 120, 0, 0, 8.0, -200.0, -85.8, 'Onyxia 65-41 %: tank slot 1 waits at the NE spot T for Cindarion and attacks him once he is selectable within 25 yd of it; his touchdown (-8.9 -212.8 -88.5, walk 1, 5 of 5 pulls) is 21.4 yd away; path-check landing->T complete 21.2 yd'),
(249, 10184, 'gate_hold', 50143, 0, 0, 42, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0, 'Onyxia: no DPS on her at or below 42 % while Cindarion fights (in combat, not evading) within 200 yd; if she dies first he evades and is unkillable for the lockout'),
(249, 10184, 'anchor', 0, 0, 0, 60, '', 0, 100, 0, '', 0, 0, 0, -22.0, -213.5, -86.5, 'Onyxia: every boss/raid move stays within 60 yd of the room centre (west edge x -82): the respawned room-mouth Warder (-113.6 -216.8, aggro 20 yd) is never chased into (walk 1: 11 of 40 deaths); the raid approach stand (-80 -211.4) is inside (58 yd)');
