-- Onyxia's Lair (map 249), walk 2d: pack 0 radius 1 -> 28 (see the plan note).
--
-- Pack 0 (entrance Warder 12129 at 49.8206 -114.607 -24.3112) keeps the plain pull of walk 2a: after resume the
-- raid walks in and pulls it in route order like every other pack; only its pull radius changes (1 -> 28: with the
-- 10 yd pack-clear margin the step covers the whole patrol loop, 37.4 yd from the spawn at its north end, so the
-- Warder is always the step's own unit; walk 2c b-2 paused 60 s with it 18 yd out of the old 11 yd circle).
-- Approach (30.9 -68 -5.8), position, the patrol block and the stand-pull removal (flags 8192 -> 0) are unchanged;
-- the plan row's source rev moves to 143052b9.
--
-- No other row changes: the export was cut with the same flags and mmaps state as 20261110120100 (mmaps only, no
-- approach-hop or floor-aware re-cut), so only the pack-0 radius and the plan row's source rev differ.
--
-- Dungeon plans for the run leader (.bot run), exported by turtle-agent (plan-export). Plan format v1; the mapping is
-- docs/guides/dungeon-run-leader.md ("From plan v1 to rows"). The plan file in turtle-agent is the single source: edit it
-- there and export again, never these rows by hand. The plan row carries the unit lists cc_priority, cc_always and kill_first
-- (columns from 20261015200000_world.sql). Never edit this file once applied; export a new one.
--
-- Idempotent: each plan's rows are deleted, then inserted. The tables come from 20261014150000_world.sql.

-- onyxias_lair: map 249, 2 legs, 39 steps
DELETE FROM `ai_dungeon_step` WHERE `plan` = 'onyxias_lair';
DELETE FROM `ai_dungeon_leg` WHERE `plan` = 'onyxias_lair';
DELETE FROM `ai_dungeon_plan` WHERE `plan` = 'onyxias_lair';
INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, `kill_first`, `source`, `comment`) VALUES
('onyxias_lair', 249, '', '', '12129', 'turtle-agent 143052b9 dungeons/onyxias_lair.json', 'plan v1, draft (prepped 2026-11-09 for core 1181dev b26c3cc; not mapped, not live-checked)');
INSERT INTO `ai_dungeon_leg` (`plan`, `leg`, `name`, `comment`) VALUES
('onyxias_lair', 0, 'tunnel', 'leave (not read by the run leader yet): 12129 at -128.7 -41: the north-passage patrol Warder (guid 2593525): its line y~-34 runs 72 yd from the route (path 155 yd); 49016 at -90.2 -30.8: the north-passage Inciters (2593508, 2593511); 49017 at -230.1 -157.'),
('onyxias_lair', 1, 'onyxia', '');
INSERT INTO `ai_dungeon_step` (`plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, `timeout_s`, `entries`, `comment`) VALUES
('onyxias_lair', 0, 0, 'move', 0, 30.89, -54.08, -4.65, 0, 0, 0, 0, 0, '', 'to'),
('onyxias_lair', 0, 1, 'move', 0, 30.9, -68, -5.8, 0, 0, 0, 0, 0, '', 'pull group 0 approach'),
('onyxias_lair', 0, 2, 'pull', 0, 49.82, -114.61, -24.31, 28, 0, 0, 0, 0, '12129', 'pull group 0'),
('onyxias_lair', 0, 3, 'move', 0, 48.799988, -101.06677, -23.947386, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 4, 'move', 0, 47.999985, -94.66677, -24.347387, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 5, 'move', 0, 47.199986, -92.26677, -25.047386, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 6, 'move', 0, 46.399986, -90.66677, -25.447386, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 7, 'move', 0, 42.666656, -85.33344, -25.947386, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 8, 'move', 0, 41.33332, -82.66677, -25.847387, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 9, 'move', 0, 39.99999, -82.13344, -25.547386, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 10, 'move', 0, 38.39999, -81.600105, -25.747387, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 11, 'move', 0, 28.533325, -78.40011, -28.547386, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 12, 'move', 0, 18.933329, -77.866776, -25.947386, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 13, 'move', 0, 14.666662, -78.13344, -25.247387, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 14, 'move', 0, 3.9999988, -81.600105, -24.947386, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 15, 'move', 0, -18.13346, -92.26677, -30.247387, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 16, 'move', 0, -33.05, -95.14, -36.06, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('onyxias_lair', 0, 17, 'pull', 0, -52.97, -96.88, -38.64, 1, 0, 0, 0, 0, '12129', 'pull group 1'),
('onyxias_lair', 0, 18, 'move', 0, -82.4, -106.13, -35.25, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('onyxias_lair', 0, 19, 'move', 0, -92, -112, -37.95, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('onyxias_lair', 0, 20, 'pull', 0, -123, -130.19, -50.37, 20, 0, 0, 8192, 0, '49017,49016,12129', 'pull group 2; stand pull'),
('onyxias_lair', 0, 21, 'move', 0, -104.53343, -122.13343, -45.047386, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 22, 'move', 0, -142.61615, -153.21036, -54.208797, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 23, 'move', 0, -161.06676, -168.26675, -58.64739, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 24, 'move', 0, -161.33342, -168.53342, -59.14739, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 25, 'move', 0, -161.86674, -170.66675, -60.447388, 0, 0, 0, 0, 0, '', 'path'),
('onyxias_lair', 0, 26, 'move', 0, -163.15, -176.31, -63.21, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('onyxias_lair', 0, 27, 'pull', 0, -166.62, -196, -66.26, 1, 0, 0, 0, 0, '12129', 'pull group 3'),
('onyxias_lair', 0, 28, 'move', 0, -163.15, -176.31, -63.21, 0, 0, 0, 0, 0, '', 'pull group 4 approach'),
('onyxias_lair', 0, 29, 'move', 0, -170.9, -192.5, -66, 0, 0, 0, 0, 0, '', 'pull group 4 approach'),
('onyxias_lair', 0, 30, 'pull', 0, -198.67, -209.11, -68.8, 10, 0, 0, 8192, 0, '12129,49016', 'pull group 4; stand pull'),
('onyxias_lair', 0, 31, 'move', 0, -149.46, -210.48, -66.7, 0, 0, 0, 0, 0, '', 'pull group 5 approach'),
('onyxias_lair', 0, 32, 'pull', 0, -181.8, -228.3, -65.7, 3, 0, 0, 8192, 0, '49017', 'pull group 5; stand pull'),
('onyxias_lair', 0, 33, 'move', 0, -149.46, -210.48, -66.7, 0, 0, 0, 0, 0, '', 'pull group 6 approach'),
('onyxias_lair', 0, 34, 'pull', 0, -111.02, -214.15, -75.03, 8, 0, 0, 8192, 0, '49016,49017,12129', 'pull group 6; stand pull'),
('onyxias_lair', 1, 0, 'move', 0, -149.46, -210.48, -66.7, 0, 0, 0, 0, 0, '', 'to'),
('onyxias_lair', 1, 1, 'move', 0, -114.73, -210.94, -72.4, 0, 0, 0, 0, 0, '', 'boss approach'),
('onyxias_lair', 1, 2, 'move', 0, -80, -211.4, -83, 0, 0, 0, 0, 0, '', 'boss approach'),
('onyxias_lair', 1, 3, 'boss', 10184, 0.69628, -211.972, -86.075, 0, 0, 0, 0, 0, '', 'boss');
