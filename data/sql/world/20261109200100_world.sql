-- Onyxia's Lair (map 249), core 1181dev b26c3cc: the boss list, the boss tactic rows for Cindarion 50143 and the
-- Onyxian Hatcher 50144, and the Onyxian Warder 12129 stand set at the corridor junction where the room-mouth pack is
-- fought. Spec: TortoiseBots-audit briefs-omp/onyxia-fresh-spec.md section 2. Never edit once applied.
--
-- ai_dungeon_boss: Onyxia 10184 keeps her row and Cindarion 50143 gets one, so the run and the wipe recovery know
-- both bosses of the changed fight. The Hatcher 50144 gets none: it is summoned twice at the 40 % landing
-- (boss_onyxia.cpp:720-721) and dies in P3, never a boss of its own.
--
-- ai_boss_tactic: walk 1 has no Onyxia row (her generic dragon flank and Onyxia fight strategy stay as they are).
-- Cindarion goes by plain threat with no fixate (boss_cindarionAI), so off-tank 1 takes him whenever he stands on
-- a non-tank (taunt_after_cast until_tank, priority 1, a new window every 4 s); his Flame Breath 33439 is a 2.0 s
-- cast the class kicks stop (interrupt) with a cone of at least +-50 deg and 28 yd (behind 30); he dies before
-- Onyxia whenever he is targetable (kill_first priority 2: if she dies first he evades and is unkillable for the
-- lockout). The two Hatchers open on random raid members with Rain of Fire 19717 and Flaming Cleave 33441, so
-- they die first in P3 (kill_first priority 1), off-tank 2 takes them off non-tanks (until_tank, priority 2, 4 s
-- windows), and non-tanks keep out of the frontal Cleave cone (behind 12). The until_tank rows need
-- AiPlayerbot.DungeonRunAuraTactics = 1.
--
-- ai_boss_stand: one anchor (id 1) at the corridor junction S (-149.46 -210.48, radius 6), where packs 5 and 6 are
-- stand-pulled. Fire Nova 20203 is instant with radius index 9 (20 yd): healers sit 34 yd out (22-40 yd from a
-- Warder anywhere in the disk, inside the 40 yd heal range) and ranged 26 yd out (20-32 yd). Every seat was
-- checked from S with turtle-agent path-check on the map 249 navmesh (offline complete, server type 0x1 ok,
-- 0.0 yd short). Bots past the seats keep their normal behaviour, the Venoxis fallback.
--
-- Idempotent: each DELETE removes only these keys. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_dungeon_boss` WHERE `map` IN (249) AND `entry` IN (10184, 50143);
REPLACE INTO `ai_dungeon_boss` (`map`, `entry`, `name`) VALUES
(249, 10184, 'Onyxia'),
(249, 50143, 'Cindarion');

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` IN (50143, 50144);
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 50143, 'taunt_after_cast', 0, 0, 1, 4, '', 0, 100, 0, 'until_tank', 0, 0, 0, 0, 0, 0, 'Cindarion: no fixate, plain threat (boss_onyxia.cpp boss_cindarionAI); he lands at Onyxia''s 65 % take-off and chases a healer; off-tank 1 takes him whenever he stands on a non-tank, a new window every 4 s; an empty threat list evades him out of the fight'),
(249, 50143, 'interrupt', 0, 33439, 0, 0, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0, 'Cindarion: Flame Breath 33439 (castingTimeIndex 5 = 2.0 s, interruptFlags 15, preventionType 1); 10 of 13 were kicked in the first look'),
(249, 50143, 'behind', 0, 33439, 0, 30, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0, 'Cindarion: Flame Breath cone on his victim, at least +-50 deg and 28 yd measured (radius index 11)'),
(249, 50143, 'kill_first', 50143, 0, 2, 0, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0, 'Cindarion first whenever he is targetable (grounded): if Onyxia dies first he evades and is unkillable for the lockout'),
(249, 50144, 'kill_first', 50144, 0, 1, 0, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0, 'Onyxian Hatcher (2 at the 40 % landing, from the egg caves): Rain of Fire 19717 and Flaming Cleave 33441 on random targets, killed first'),
(249, 50144, 'taunt_after_cast', 0, 0, 2, 4, '', 0, 100, 0, 'until_tank', 0, 0, 0, 0, 0, 0, 'Onyxian Hatcher: opens on a random raid member (AttackStart random); off-tank 2 takes it off a non-tank, a new window every 4 s'),
(249, 50144, 'behind', 0, 33441, 0, 12, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0, 'Onyxian Hatcher: Flaming Cleave 33441 frontal cone (target type 24, radius index 13 = 10 yd)');

DELETE FROM `ai_boss_stand` WHERE `map` = 249 AND `boss_entry` = 12129;
INSERT INTO `ai_boss_stand` (`map`, `boss_entry`, `anchor`, `anchor_x`, `anchor_y`, `anchor_r`, `role`, `window_spell`, `aura`, `side`, `seq`, `x`, `y`, `z`, `dx`, `dy`, `slots`, `hold_ms`, `release_anchor`, `window_signal`, `comment`) VALUES
-- Anchor 1: the corridor junction where packs 5 and 6 are stand-pulled, 6 yd radius. Fire Nova 20203 is instant with radius
-- index 9 (20 yd): healers 34 yd out (22-40 yd from a Warder anywhere in the disk), ranged 26 yd out (20-32 yd).
(249, 12129, 1, -149.46, -210.48, 6, 'healer', 0, 0, 0, 0, -161.1, -178.5, -63.7, 0, 0, 1, 8000, 0, 'cast', 'Warder healer seat 0: 34 yd, bearing 110 deg from the anchor, path-check ok'),
(249, 12129, 1, -149.46, -210.48, 6, 'healer', 0, 0, 0, 1, -166.5, -181.0, -64.6, 0, 0, 1, 8000, 0, 'cast', 'Warder healer seat 1: 34 yd, 120 deg, path-check ok'),
(249, 12129, 1, -149.46, -210.48, 6, 'healer', 0, 0, 0, 2, -171.3, -184.4, -65.2, 0, 0, 1, 8000, 0, 'cast', 'Warder healer seat 2: 34 yd, 130 deg, path-check ok'),
(249, 12129, 1, -149.46, -210.48, 6, 'healer', 0, 0, 0, 3, -175.5, -188.6, -65.8, 0, 0, 1, 8000, 0, 'cast', 'Warder healer seat 3: 34 yd, 140 deg, path-check ok'),
(249, 12129, 1, -149.46, -210.48, 6, 'healer', 0, 0, 0, 4, -178.9, -193.5, -66.3, 0, 0, 1, 8000, 0, 'cast', 'Warder healer seat 4: 34 yd, 150 deg, path-check ok'),
(249, 12129, 1, -149.46, -210.48, 6, 'healer', 0, 0, 0, 5, -181.4, -198.9, -66.5, 0, 0, 1, 8000, 0, 'cast', 'Warder healer seat 5: 34 yd, 160 deg, path-check ok'),
(249, 12129, 1, -149.46, -210.48, 6, 'healer', 0, 0, 0, 6, -182.9, -204.6, -66.3, 0, 0, 1, 8000, 0, 'cast', 'Warder healer seat 6: 34 yd, 170 deg, path-check ok'),
(249, 12129, 1, -149.46, -210.48, 6, 'healer', 0, 0, 0, 7, -183.5, -210.5, -65.6, 0, 0, 1, 8000, 0, 'cast', 'Warder healer seat 7: 34 yd, 180 deg, path-check ok'),
(249, 12129, 1, -149.46, -210.48, 6, 'healer', 0, 0, 0, 8, -182.9, -216.4, -65.6, 0, 0, 1, 8000, 0, 'cast', 'Warder healer seat 8: 34 yd, 190 deg, path-check ok'),
(249, 12129, 1, -149.46, -210.48, 6, 'ranged', 0, 0, 0, 0, -162.5, -188.0, -64.9, 0, 0, 1, 8000, 0, 'cast', 'Warder ranged seat 0: 26 yd, 120 deg, path-check ok'),
(249, 12129, 1, -149.46, -210.48, 6, 'ranged', 0, 0, 0, 1, -167.8, -192.1, -65.5, 0, 0, 1, 8000, 0, 'cast', 'Warder ranged seat 1: 26 yd, 135 deg, path-check ok'),
(249, 12129, 1, -149.46, -210.48, 6, 'ranged', 0, 0, 0, 2, -172.0, -197.5, -66.0, 0, 0, 1, 8000, 0, 'cast', 'Warder ranged seat 2: 26 yd, 150 deg, path-check ok'),
(249, 12129, 1, -149.46, -210.48, 6, 'ranged', 0, 0, 0, 3, -174.6, -203.8, -66.1, 0, 0, 1, 8000, 0, 'cast', 'Warder ranged seat 3: 26 yd, 165 deg, path-check ok'),
(249, 12129, 1, -149.46, -210.48, 6, 'ranged', 0, 0, 0, 4, -175.5, -210.5, -64.6, 0, 0, 1, 8000, 0, 'cast', 'Warder ranged seat 4: 26 yd, 180 deg, path-check ok');
