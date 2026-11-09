-- Onyxia's Lair (map 249), core 1181dev b26c3cc: the fresh two-leg plan for the changed fight.
--
-- What changed since the prep export (20261029130000, turtle-agent prep/raid-ony 9015cf46: one leg
-- `onyxia`, five single-Warder packs, no approach stands but one per pack): the tunnel holds new trash
-- (Onyxian Inciter 49016 and Onyxian Flamespawn 49017 next to the old Onyxian Warder 12129), so the plan
-- is two legs (`tunnel` with seven packs, then the boss leg `onyxia`): pack 2 folds the main-path pair and
-- the patrol Warder 52052 into one stand-pull pack of radius 20, pack 4 folds the last-bend Warder and two
-- Inciters into one stand-pull pack of radius 10, pack 5 is the lone Flamespawn by the corridor, and pack 6
-- is the room-mouth pack, fought at the corridor junction S (-149.46 -210.48 -66.7), about 150 yd from
-- Onyxia. Packs 0, 2, 4, 5 and 6 carry the stand-pull flag (8192, clear_yd 0: the run leader's 40); the
-- plan row carries kill_first '12129' (the Warder's instant Fire Nova 20203 killed 10 of 39 bots in the
-- first look). The five `leave` spawns (north passage, west alcove, ledge Warder, Axelus) ride the leg
-- row's comment; the four patrol paths stay in the plan file. Pack 0 stands on the entrance platform
-- (30.9 -68.0 -5.8, 53.6 yd from the pull and 27 yd 3D from the nearest lane point): the raid covers the
-- pull from outside the Warder lane and Fire Nova reach. Leg 1 carries `to` (-149.46 -210.48 -66.7, the
-- corridor junction): the loader requires `to` on every leg.
--
-- plan-check (turtle-agent plan-check --dir dungeons --mmaps <map 249 mmaps> --spawns
-- tools/dungeongen/testdata/map249): ok, 28 points on the navmesh, 0 failures, 1 run-leader hop notice
-- (the 81 yd walk from the boss stand to her) and 4 estimated-patrol warnings; the lints add 55 findings
-- (1 fails the check: the cut hop's server-NOPATH pair at step 32, also on the prep baseline in kind).
--
-- Dungeon plans for the run leader (.bot run), exported by turtle-agent (plan-export). Plan format v1; the mapping is
-- docs/guides/dungeon-run-leader.md ("From plan v1 to rows"). The plan file in turtle-agent is the single source: edit it
-- there and export again, never these rows by hand. The plan row carries the unit lists cc_priority, cc_always and kill_first
-- (columns from 20261015200000_world.sql). Never edit this file once applied; export a new one.
--
-- Idempotent: each plan's rows are deleted, then inserted. The tables come from 20261014150000_world.sql.

-- onyxias_lair: map 249, 2 legs, 37 steps
DELETE FROM `ai_dungeon_step` WHERE `plan` = 'onyxias_lair';
DELETE FROM `ai_dungeon_leg` WHERE `plan` = 'onyxias_lair';
DELETE FROM `ai_dungeon_plan` WHERE `plan` = 'onyxias_lair';
INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, `kill_first`, `source`, `comment`) VALUES
('onyxias_lair', 249, '', '', '12129', 'turtle-agent 7489d2d74678d7daeecea8c524445bb767fae4a7 dungeons/o', 'plan v1, draft (prepped 2026-11-09 for core 1181dev b26c3cc; not mapped, not live-checked)');
INSERT INTO `ai_dungeon_leg` (`plan`, `leg`, `name`, `comment`) VALUES
('onyxias_lair', 0, 'tunnel', 'leave (not read by the run leader yet): 12129 at -128.7 -41: the north-passage patrol Warder (guid 2593525): its line y~-34 runs 72 yd from the route (path 155 yd); 49016 at -90.2 -30.8: the north-passage Inciters (2593508, 2593511); 49017 at -230.1 -157.'),
('onyxias_lair', 1, 'onyxia', '');
INSERT INTO `ai_dungeon_step` (`plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, `timeout_s`, `entries`, `comment`) VALUES
('onyxias_lair', 0, 0, 'move', 0, 30.89, -54.08, -4.65, 0, 0, 0, 0, 0, '', 'to'),
('onyxias_lair', 0, 1, 'move', 0, 30.9, -68, -5.8, 0, 0, 0, 0, 0, '', 'pull group 0 approach'),
('onyxias_lair', 0, 2, 'pull', 0, 49.82, -114.61, -24.31, 1, 0, 0, 8192, 0, '12129', 'pull group 0; stand pull'),
('onyxias_lair', 0, 3, 'move', 0, 28.466658, -92.53344, -14.987859, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 4, 'move', 0, 29.266659, -102.13344, -18.196905, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 5, 'move', 0, 31.095018, -107.54815, -20.482285, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 6, 'move', 0, 35.628353, -111.01482, -22.105625, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 7, 'move', 0, 39.89502, -109.41482, -23.29663, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 8, 'move', 0, 46.666653, -109.66676, -24.060684, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 9, 'move', 0, 49.948036, -103.83841, -24.268822, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 10, 'move', 0, 48.369217, -97.62061, -24.108925, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 11, 'move', 0, 50.199986, -92.26677, -24.896927, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 12, 'move', 0, 44.33332, -82.66677, -26.087387, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 13, 'move', 0, 28.533325, -75.40011, -28.699055, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 14, 'move', 0, 15.814713, -75.3618, -25.712662, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 15, 'move', 0, 0.9999988, -81.600105, -25.431227, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 16, 'move', 0, -16.985409, -95.03841, -31.05208, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 17, 'move', 0, -33.05, -95.14, -36.06, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('onyxias_lair', 0, 18, 'pull', 0, -52.97, -96.88, -38.64, 1, 0, 0, 0, 0, '12129', 'pull group 1'),
('onyxias_lair', 0, 19, 'move', 0, -82.4, -106.13, -35.25, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('onyxias_lair', 0, 20, 'move', 0, -92, -112, -37.95, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('onyxias_lair', 0, 21, 'pull', 0, -123, -130.19, -50.37, 20, 0, 0, 8192, 0, '49017,49016,12129', 'pull group 2; stand pull'),
('onyxias_lair', 0, 22, 'move', 0, -107.53343, -122.13343, -46.986122, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 23, 'move', 0, -164.06676, -168.26675, -60.278797, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 24, 'move', 0, -163.15, -176.31, -63.21, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('onyxias_lair', 0, 25, 'pull', 0, -166.62, -196, -66.26, 1, 0, 0, 0, 0, '12129', 'pull group 3'),
('onyxias_lair', 0, 26, 'move', 0, -163.15, -176.31, -63.21, 0, 0, 0, 0, 0, '', 'pull group 4 approach'),
('onyxias_lair', 0, 27, 'move', 0, -170.9, -192.5, -66, 0, 0, 0, 0, 0, '', 'pull group 4 approach'),
('onyxias_lair', 0, 28, 'pull', 0, -198.67, -209.11, -68.8, 10, 0, 0, 8192, 0, '12129,49016', 'pull group 4; stand pull'),
('onyxias_lair', 0, 29, 'move', 0, -149.46, -210.48, -66.7, 0, 0, 0, 0, 0, '', 'pull group 5 approach'),
('onyxias_lair', 0, 30, 'pull', 0, -181.8, -228.3, -65.7, 3, 0, 0, 8192, 0, '49017', 'pull group 5; stand pull'),
('onyxias_lair', 0, 31, 'move', 0, -149.46, -210.48, -66.7, 0, 0, 0, 0, 0, '', 'pull group 6 approach'),
('onyxias_lair', 0, 32, 'pull', 0, -111.02, -214.15, -75.03, 8, 0, 0, 8192, 0, '49016,49017,12129', 'pull group 6; stand pull'),
('onyxias_lair', 1, 0, 'move', 0, -149.46, -210.48, -66.7, 0, 0, 0, 0, 0, '', 'to'),
('onyxias_lair', 1, 1, 'move', 0, -114.73, -210.94, -72.4, 0, 0, 0, 0, 0, '', 'boss approach'),
('onyxias_lair', 1, 2, 'move', 0, -80, -211.4, -83, 0, 0, 0, 0, 0, '', 'boss approach'),
('onyxias_lair', 1, 3, 'boss', 10184, 0.69628, -211.972, -86.075, 0, 0, 0, 0, 0, '', 'boss');
