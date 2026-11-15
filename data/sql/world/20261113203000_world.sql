-- Ragefire Chasm (map 389), exported again for one leg: the party holds on the deck centre instead of the deck's west end,
-- and the tunnel lava's shelf under the deck is an avoid box (turtle-agent fix/rfc-bridge-lava 66881ac0,
-- dungeons/ragefire_chasm.json). The bot-led run of 2026-11-13 (TortoiseBots-audit/tests/bug-hunt-1113/RFC-BRIDGE.md) held
-- the party at leg 1 seq 1, 11.8 yd from the only walkable cell under the tunnel's lava: a NAV_GROUND shelf at x -103..-99,
-- y -27..-23.5, floor z -30.5..-29.7, linked to the deck and 0.1 to 0.4 yd under the magma at z -30.1, so a follower put on
-- it by a core follow slot dies in one tick. Export, with the options of 20261104160000's header:
--   turtle-agent plan-export --dir dungeons --allow-draft --gm-cross-direct --floor-aware --approach-hops --mmaps <the
--     server's mmaps> --rev 66881ac0 --stamp 20261113203000 ragefire_chasm
-- NO ROW IS HAND-SET. Plan check (turtle-agent 2ad20ad4 build, map 389 mmaps): "ok, 116 points on the map 389 navmesh",
-- output identical to the plan before the change. 9 legs, 84 steps (was 83).
--
-- WHAT CHANGES IN THE ROWS (everything else is row for row 20261104160000):
--   * plan row: source rev ca6fb6d -> 66881ac0.
--   * leg 1, seq 1 ('pull group 0 approach'): -96.36 -36.33 -28.11 -> -90 -36 -24.7, the deck centre 15 yd from the shelf
--     (the mesh floor at that x,y is -24.7; the -23.1 first written was 1.6 yd above it and the off-floor lint refused it).
--   * leg 2 seq 11 ('approach path' -244.92 -10.73 -62.07) is not this change: the 20261104160000 rows came from ca6fb6d,
--     which cut that hop differently; an export of origin/dev's plan file with this binary writes the row too.
-- The avoid box is not in the rows (the run leader does not know avoid boxes, and the export's route found no hop of the
-- rows to cut for it): it keeps the client's planner off the shelf, and the server side needs the follow/chase guard.
--
-- Dungeon plans for the run leader (.bot run), exported by turtle-agent (plan-export). Plan format v1; the mapping is
-- docs/guides/dungeon-run-leader.md ("From plan v1 to rows"). The plan file in turtle-agent is the single source: edit it
-- there and export again, never these rows by hand. The plan row carries the unit lists cc_priority, cc_always and kill_first
-- (columns from 20261015200000_world.sql). Never edit this file once applied; export a new one.
--
-- Idempotent: each plan's rows are deleted, then inserted. The tables come from 20261014150000_world.sql.

-- ragefire_chasm: map 389, 9 legs, 84 steps
DELETE FROM `ai_dungeon_step` WHERE `plan` = 'ragefire_chasm';
DELETE FROM `ai_dungeon_leg` WHERE `plan` = 'ragefire_chasm';
DELETE FROM `ai_dungeon_plan` WHERE `plan` = 'ragefire_chasm';
INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, `kill_first`, `source`, `comment`) VALUES
('ragefire_chasm', 389, '11319,11324,11322', '11319,11324', '11319', 'turtle-agent 66881ac0 dungeons/ragefire_chasm.json', 'plan v1, mapped (not live-checked)');
INSERT INTO `ai_dungeon_leg` (`plan`, `leg`, `name`, `comment`) VALUES
('ragefire_chasm', 0, 'tunnel', ''),
('ragefire_chasm', 1, 'tunnel_end', ''),
('ragefire_chasm', 2, 'trogg_cave', ''),
('ragefire_chasm', 3, 'oggleflint', ''),
('ragefire_chasm', 4, 'trogg_west', ''),
('ragefire_chasm', 5, 'molten_cave', ''),
('ragefire_chasm', 6, 'taragaman', ''),
('ragefire_chasm', 7, 'jergosh', ''),
('ragefire_chasm', 8, 'bazzalan', '');
INSERT INTO `ai_dungeon_step` (`plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, `timeout_s`, `entries`, `comment`) VALUES
('ragefire_chasm', 0, 0, 'move', 0, 0.8, -8.23, -15.53, 0, 0, 0, 0, 0, '', 'to'),
('ragefire_chasm', 0, 1, 'pull', 0, -15.99, -50.17, -21.78, 11, 0, 0, 0, 0, '11320', 'pull group 0'),
('ragefire_chasm', 0, 2, 'pull', 0, -23.8, -62.16, -21.37, 11, 0, 0, 0, 0, '11320', 'pull group 1'),
('ragefire_chasm', 0, 3, 'pull', 0, -36.46, -42.09, -21.77, 13, 0, 0, 0, 0, '11321', 'pull group 2'),
('ragefire_chasm', 0, 4, 'pull', 0, -45.03, -27.76, -21.29, 12, 0, 0, 0, 0, '11320', 'pull group 3'),
('ragefire_chasm', 0, 5, 'pull', 0, -70.61, -32.83, -18.49, 15, 0, 0, 0, 0, '11320', 'pull group 4'),
('ragefire_chasm', 1, 0, 'move', 0, -80.5, -34.2, -20.27, 0, 0, 0, 0, 0, '', 'to'),
('ragefire_chasm', 1, 1, 'move', 0, -90, -36, -24.7, 0, 0, 0, 0, 0, '', 'pull group 0 approach'),
('ragefire_chasm', 1, 2, 'pull', 0, -106.32, -37.66, -30.04, 12, 0, 0, 0, 0, '11320', 'pull group 0'),
('ragefire_chasm', 1, 3, 'pull', 0, -129.13, -35.16, -33.26, 12, 0, 0, 0, 0, '11320', 'pull group 1'),
('ragefire_chasm', 2, 0, 'move', 0, -139.1, -34.6, -32.89, 0, 0, 0, 0, 0, '', 'to'),
('ragefire_chasm', 2, 1, 'move', 0, -163.06, -33.17, -39.66, 0, 0, 0, 0, 0, '', 'pull group 0 approach'),
('ragefire_chasm', 2, 2, 'pull', 0, -181.51, -32.07, -44.43, 20, 0, 0, 0, 0, '11318', 'pull group 0'),
('ragefire_chasm', 2, 3, 'move', 0, -205, -34, -49.9, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('ragefire_chasm', 2, 4, 'pull', 0, -238.33, -35.95, -56.44, 12, 0, 0, 0, 0, '11318', 'pull group 1'),
('ragefire_chasm', 2, 5, 'move', 0, -221.2, -34.7, -54.7, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('ragefire_chasm', 2, 6, 'pull', 0, -263.92, -34.62, -60.63, 12, 0, 0, 0, 0, '11319,11318', 'pull group 2'),
('ragefire_chasm', 2, 7, 'pull', 0, -263.11, -14.45, -60.97, 12, 0, 0, 0, 0, '11318', 'pull group 3'),
('ragefire_chasm', 2, 8, 'pull', 0, -289.53, -21.1, -61.52, 12, 0, 0, 0, 0, '11318', 'pull group 4'),
('ragefire_chasm', 2, 9, 'move', 0, -273.93338, -11.466794, -62.75936, 0, 0, 0, 0, 0, '', 'approach path'),
('ragefire_chasm', 2, 10, 'move', 0, -266.46674, -9.333462, -63.599895, 0, 0, 0, 0, 0, '', 'approach path'),
('ragefire_chasm', 2, 11, 'move', 0, -244.92, -10.73, -62.07, 0, 0, 0, 0, 0, '', 'approach path'),
('ragefire_chasm', 2, 12, 'pull', 0, -215.45, -15.93, -61.8, 6, 0, 0, 0, 0, '11318', 'pull group 5'),
('ragefire_chasm', 2, 13, 'pull', 0, -211.15, -4.54, -61.76, 6, 0, 0, 0, 0, '11318,11319', 'pull group 6'),
('ragefire_chasm', 2, 14, 'pull', 0, -183.39, 2.8, -56.81, 12, 0, 0, 0, 0, '11318', 'pull group 7'),
('ragefire_chasm', 3, 0, 'move', 0, -176.34, 4.8, -53.46, 0, 0, 0, 0, 0, '', 'to'),
('ragefire_chasm', 3, 1, 'move', 0, -164, 3.6, -46.91, 0, 0, 0, 0, 0, '', 'pull group 0 approach'),
('ragefire_chasm', 3, 2, 'pull', 0, -150.06, 2.18, -39.87, 12, 0, 0, 0, 0, '11318,11319', 'pull group 0'),
('ragefire_chasm', 3, 3, 'boss', 11517, -147.55, 38.71, -38.8, 0, 0, 0, 0, 0, '', 'boss'),
('ragefire_chasm', 4, 0, 'move', 0, -148.98537, 5.228359, -39.389725, 0, 0, 0, 0, 0, '', 'path'),
('ragefire_chasm', 4, 1, 'move', 0, -192.00008, 3, -59.463657, 0, 0, 0, 0, 0, '', 'path'),
('ragefire_chasm', 4, 2, 'move', 0, -232.61479, -10.771767, -61.686268, 0, 0, 0, 0, 0, '', 'path'),
('ragefire_chasm', 4, 3, 'move', 0, -266.46674, -9.333462, -63.599895, 0, 0, 0, 0, 0, '', 'path'),
('ragefire_chasm', 4, 4, 'move', 0, -271, -11, -63, 0, 0, 0, 0, 0, '', 'to'),
('ragefire_chasm', 4, 5, 'pull', 0, -280.9, -49.18, -60.93, 7, 0, 0, 0, 0, '11319', 'pull group 0'),
('ragefire_chasm', 4, 6, 'pull', 0, -290.81, -47.08, -60.89, 7, 0, 0, 0, 0, '11318', 'pull group 1'),
('ragefire_chasm', 4, 7, 'move', 0, -283.3, -21.24, -60.59, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('ragefire_chasm', 4, 8, 'move', 0, -300, -13.56, -53.99, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('ragefire_chasm', 4, 9, 'pull', 0, -296.56, 7.55, -47.53, 12, 0, 0, 0, 0, '11320', 'pull group 2'),
('ragefire_chasm', 4, 10, 'pull', 0, -272.52, 8.66, -50, 17, 0, 0, 0, 0, '11320', 'pull group 3'),
('ragefire_chasm', 4, 11, 'pull', 0, -252.09, 7.16, -50.03, 12, 0, 0, 0, 0, '11321', 'pull group 4'),
('ragefire_chasm', 4, 12, 'move', 0, -228.16, 8.93, -43.64, 0, 0, 0, 0, 0, '', 'pull group 5 approach'),
('ragefire_chasm', 4, 13, 'move', 0, -204.22, 10.7, -36.99, 0, 0, 0, 0, 0, '', 'pull group 5 approach'),
('ragefire_chasm', 4, 14, 'pull', 0, -168.54, 13.33, -29.62, 12, 0, 0, 0, 0, '11320', 'pull group 5'),
('ragefire_chasm', 5, 0, 'move', 0, -140, 8, -20.66, 0, 0, 0, 0, 0, '', 'to'),
('ragefire_chasm', 5, 1, 'move', 0, -124.081474, 16.16169, -18.978693, 0, 0, 0, 0, 0, '', 'approach path'),
('ragefire_chasm', 5, 2, 'move', 0, -122.481476, 18.56169, -18.485844, 0, 0, 0, 0, 0, '', 'approach path'),
('ragefire_chasm', 5, 3, 'move', 0, -119.46677, 22.06666, -18.384924, 0, 0, 0, 0, 0, '', 'approach path'),
('ragefire_chasm', 5, 4, 'pull', 0, -111.34, 52.05, -18.69, 13, 0, 0, 0, 0, '11321', 'pull group 0'),
('ragefire_chasm', 5, 5, 'pull', 0, -94.6, 54.19, -17.98, 5, 0, 0, 0, 0, '11321', 'pull group 1'),
('ragefire_chasm', 5, 6, 'pull', 0, -95.81, 45.34, -17.41, 5, 0, 0, 0, 0, '11320', 'pull group 2'),
('ragefire_chasm', 5, 7, 'pull', 0, -124.16, 63.96, -21.21, 9, 0, 0, 0, 0, '11321', 'pull group 3'),
('ragefire_chasm', 5, 8, 'pull', 0, -111.2, 76.36, -20.14, 11, 0, 0, 0, 0, '11321', 'pull group 4'),
('ragefire_chasm', 5, 9, 'pull', 0, -126.51, 77.82, -21.6, 9, 0, 0, 0, 0, '11321,11320', 'pull group 5'),
('ragefire_chasm', 5, 10, 'pull', 0, -144.94, 79.15, -20.98, 14, 0, 0, 0, 0, '11321', 'pull group 6'),
('ragefire_chasm', 5, 11, 'pull', 0, -176.65, 75.29, -21.9, 15, 0, 0, 0, 0, '11321', 'pull group 7'),
('ragefire_chasm', 6, 0, 'move', 0, -168.56, 71.69, -21, 0, 0, 0, 0, 0, '', 'to'),
('ragefire_chasm', 6, 1, 'pull', 0, -202.43, 93.54, -25.15, 12, 0, 0, 0, 0, '11322,11323', 'pull group 0'),
('ragefire_chasm', 6, 2, 'pull', 0, -221.38, 90.92, -24.94, 8, 0, 0, 0, 0, '11323', 'pull group 1'),
('ragefire_chasm', 6, 3, 'pull', 0, -199.29, 107.95, -24.65, 9, 0, 0, 0, 0, '11323', 'pull group 2'),
('ragefire_chasm', 6, 4, 'pull', 0, -185.92, 157.46, -25.19, 8, 0, 0, 0, 0, '11322,11324,8996', 'pull group 3'),
('ragefire_chasm', 6, 5, 'pull', 0, -199.08, 155.12, -25.14, 8, 0, 0, 0, 0, '11323', 'pull group 4'),
('ragefire_chasm', 6, 6, 'boss', 11520, -244.74, 150.09, -18.75, 0, 0, 0, 0, 0, '', 'boss'),
('ragefire_chasm', 7, 0, 'move', 0, -259, 150.4, -18.84, 0, 0, 0, 0, 0, '', 'to'),
('ragefire_chasm', 7, 1, 'pull', 0, -263.52, 150.47, -19, 15, 0, 0, 0, 0, '11323', 'pull group 0'),
('ragefire_chasm', 7, 2, 'pull', 0, -283.68, 148.18, -25.31, 10, 0, 0, 0, 0, '11323', 'pull group 1'),
('ragefire_chasm', 7, 3, 'pull', 0, -297.03, 150.63, -25.09, 10, 0, 0, 0, 0, '11322,11324,8996', 'pull group 2'),
('ragefire_chasm', 7, 4, 'pull', 0, -298.31, 199.25, -25.03, 12, 0, 0, 0, 0, '11322,11324,8996', 'pull group 3'),
('ragefire_chasm', 7, 5, 'pull', 0, -281.18, 216.65, -25.02, 12, 0, 0, 0, 0, '11322,11324,8996', 'pull group 4'),
('ragefire_chasm', 7, 6, 'pull', 0, -313.27, 222.55, -22.7, 10, 0, 0, 0, 0, '11322,11324,8996', 'pull group 5'),
('ragefire_chasm', 7, 7, 'pull', 0, -333.07, 217.45, -20.1, 7, 0, 0, 0, 0, '11323', 'pull group 6'),
('ragefire_chasm', 7, 8, 'pull', 0, -343.41, 210.73, -21.44, 7, 0, 0, 0, 0, '11322', 'pull group 7'),
('ragefire_chasm', 7, 9, 'boss', 11518, -376.81, 209.22, -21.8, 0, 0, 0, 0, 0, '', 'boss'),
('ragefire_chasm', 8, 0, 'move', 0, -345, 222, -20.54, 0, 0, 0, 0, 0, '', 'to'),
('ragefire_chasm', 8, 1, 'move', 0, -335.99, 232.26, -15.12, 0, 0, 0, 0, 0, '', 'pull group 0 approach'),
('ragefire_chasm', 8, 2, 'pull', 0, -320.42, 252.1, -11.51, 12, 0, 0, 0, 0, '11323', 'pull group 0'),
('ragefire_chasm', 8, 3, 'pull', 0, -297.26, 253.85, -13.93, 15, 0, 0, 0, 0, '11322', 'pull group 1'),
('ragefire_chasm', 8, 4, 'pull', 0, -342.31, 256.42, -7.98, 12, 0, 0, 0, 0, '11322', 'pull group 2'),
('ragefire_chasm', 8, 5, 'pull', 0, -372.64, 258.72, -4.9, 8, 0, 0, 0, 0, '11322,11324,8996', 'pull group 3'),
('ragefire_chasm', 8, 6, 'pull', 0, -386.57, 245.73, -4.85, 8, 0, 0, 0, 0, '11324,11322,8996', 'pull group 4'),
('ragefire_chasm', 8, 7, 'pull', 0, -400.44, 216.18, -1.01, 12, 0, 0, 0, 0, '11322', 'pull group 5'),
('ragefire_chasm', 8, 8, 'pull', 0, -408.64, 179.36, 6.64, 10, 0, 0, 0, 0, '11323', 'pull group 6'),
('ragefire_chasm', 8, 9, 'pull', 0, -396.31, 150.83, 7.82, 9, 0, 0, 0, 0, '11322', 'pull group 7'),
('ragefire_chasm', 8, 10, 'boss', 11519, -384.87, 146.04, 7.82, 0, 0, 0, 0, 0, '', 'boss');
