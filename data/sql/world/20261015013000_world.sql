-- Zul'Farrak (map 209), bots-only observe run. Scope: entrance -> Antu'sul (8127) -> Theka the Martyr (7272) -> Hydromancer
-- Velratha (7795), the stretch turtle-agent live-checked (PR #178, ZF-1 run 6 2026-10-15: all three bosses dead). Exported from
-- turtle-agent 4765574 plus the ZF-1 run 6 edits (Velratha groups at 1591 1047.7 and 1626.5 1136 dropped; a hop to 1633 868
-- before the Shadowhunter-pack walk, as the leg antusul_c), with the exporter's options --mmaps --snap --end-stop:
--   * every move point and pull/boss point was checked against the map 209 navmesh (none needed moving);
--   * a hop of more than 60 yd is cut into 'path' moves along the navmesh, also the walk to a far pull or boss (stops 30 yd short);
--   * a pull group with a patrol says so in its comment (the run leader has no patrol column yet);
--   * the last leg 'STOP not supported (gong)' holds a ritual step: the run leader pauses on it (ritual is not run yet) and the
--     status line names the leg. Gahz'rilla's gong event (Mallet of Zul'Farrak) and everything after Velratha are not in the plan.
-- Plan v1 'skip' (scarabs 7269, rares 10080/10082), cc_priority/cc_always and patrol waits are not carried over.
--
-- Dungeon plans for the run leader (.bot run), exported by turtle-agent (plan-export). Plan format v1; the mapping is
-- docs/guides/dungeon-run-leader.md ("From plan v1 to rows"). The plan file in turtle-agent is the single source: edit it
-- there and export again, never these rows by hand. Never edit this file once applied; export a new one.
--
-- Idempotent: each plan's rows are deleted, then inserted. The tables come from 20261014150000_world.sql.

-- zulfarrak: map 209, 10 legs, 86 steps
DELETE FROM `ai_dungeon_step` WHERE `plan` = 'zulfarrak';
DELETE FROM `ai_dungeon_leg` WHERE `plan` = 'zulfarrak';
DELETE FROM `ai_dungeon_plan` WHERE `plan` = 'zulfarrak';
INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `source`, `comment`) VALUES
('zulfarrak', 209, 'turtle-agent 4765574 dungeons/zulfarrak.json', 'plan v1, live-checked');
INSERT INTO `ai_dungeon_leg` (`plan`, `leg`, `name`, `comment`) VALUES
('zulfarrak', 0, 'antusul_a', ''),
('zulfarrak', 1, 'antusul_a2', ''),
('zulfarrak', 2, 'antusul_a3', ''),
('zulfarrak', 3, 'antusul_a4', ''),
('zulfarrak', 4, 'antusul', ''),
('zulfarrak', 5, 'antusul_c', ''),
('zulfarrak', 6, 'antusul_b', ''),
('zulfarrak', 7, 'theka', ''),
('zulfarrak', 8, 'velratha', ''),
('zulfarrak', 9, 'STOP not supported (gong)', 'Gahz''rilla''s gong event (Mallet of Zul''Farrak) is not in the run leader; the live-proven plan ends at Velratha');
INSERT INTO `ai_dungeon_step` (`plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, `timeout_s`, `entries`, `comment`) VALUES
('zulfarrak', 0, 0, 'move', 0, 1252.3, 836.4, 9.1, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 0, 1, 'pull', 0, 1263.13, 842.53, 9.13, 15, 0, 0, 0, 0, '5649,5648', 'pull group 0'),
('zulfarrak', 0, 2, 'pull', 0, 1295.37, 810.53, 11.3, 30, 0, 0, 0, 0, '5649,5648,5650', 'pull group 1'),
('zulfarrak', 1, 0, 'move', 0, 1289.9, 816.1, 9.4, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 1, 1, 'move_direct', 0, 1297.1, 827.9, 9.3, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 1, 2, 'move_direct', 0, 1310, 834.3, 9.5, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 1, 3, 'move', 0, 1310, 834.3, 9.5, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 1, 4, 'pull', 0, 1324.33, 839.5, 10, 30, 0, 0, 0, 0, '5649,5648,5650', 'pull group 0'),
('zulfarrak', 1, 5, 'pull', 0, 1361.35, 818.6, 9, 15, 0, 0, 0, 0, '5649', 'pull group 1'),
('zulfarrak', 1, 6, 'pull', 0, 1406.5, 813.05, 8.9, 60, 0, 0, 0, 0, '5649', 'pull group 2'),
('zulfarrak', 1, 7, 'pull', 0, 1428.1, 796.63, 11.47, 15, 0, 0, 0, 0, '5648', 'pull group 3'),
('zulfarrak', 2, 0, 'move', 0, 1420.7, 801.1, 11, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 2, 1, 'move_direct', 0, 1433.9, 804.7, 10.3, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 2, 2, 'move_direct', 0, 1442.5, 804.7, 12.7, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 2, 3, 'move_direct', 0, 1450.1, 805.9, 12.9, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 2, 4, 'move', 0, 1460.5, 808.5, 10.8, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 2, 5, 'pull', 0, 1491.13, 824.87, 9.07, 30, 0, 0, 0, 0, '5650,5649', 'pull group 0'),
('zulfarrak', 3, 0, 'move', 0, 1506, 844.9, 10.2, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 3, 1, 'pull', 0, 1506.75, 838.5, 9.45, 15, 0, 0, 0, 0, '5648', 'pull group 0'),
('zulfarrak', 4, 0, 'move', 0, 1506, 844.9, 10.2, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 4, 1, 'move_direct', 0, 1520, 840, 9.5, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 4, 2, 'move_direct', 0, 1535, 834, 9, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 4, 3, 'move', 0, 1535, 834, 9, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 4, 4, 'pull', 0, 1530.05, 823.93, 9.07, 30, 0, 0, 0, 0, '5649,5648', 'pull group 0'),
('zulfarrak', 4, 5, 'pull', 0, 1545.6, 837.05, 8.9, 30, 0, 0, 0, 0, '5649,5648', 'pull group 1'),
('zulfarrak', 4, 6, 'pull', 0, 1583.83, 850.53, 10.3, 15, 0, 0, 0, 0, '5650,5648', 'pull group 2'),
('zulfarrak', 4, 7, 'pull', 0, 1633.3, 868.3, 10.5, 15, 0, 0, 0, 0, '5650', 'pull group 3'),
('zulfarrak', 4, 8, 'pull', 0, 1643.65, 904, 9.4, 10, 0, 0, 0, 0, '5648,7247', 'pull group 4 (patrol 7246: 45 points, 126 s round)'),
('zulfarrak', 5, 0, 'move', 0, 1633, 868, 9, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 5, 1, 'pull', 0, 1685.03, 876.3, 9, 20, 0, 0, 0, 0, '5649,5648', 'pull group 0 (patrol 7246: 45 points, 126 s round)'),
('zulfarrak', 5, 2, 'move', 0, 1675.9542, 827.1307, 9.016994, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 5, 3, 'move', 0, 1674.9333, 821.5999, 9.025752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 5, 4, 'move', 0, 1676.5333, 818.13324, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 5, 5, 'move', 0, 1682.4, 807.9999, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 5, 6, 'pull', 0, 1713.7, 784.1, 17.3, 15, 0, 0, 0, 0, '5649', 'pull group 1'),
('zulfarrak', 5, 7, 'pull', 0, 1776.9, 813.7, 23.2, 15, 0, 0, 0, 0, '5650', 'pull group 2'),
('zulfarrak', 6, 0, 'move', 0, 1769.03, 802.98, 24.04, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 6, 1, 'move', 0, 1787.8, 790.6, 17.7, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 6, 2, 'pull', 0, 1793.5, 753.4, 14.3, 50, 0, 0, 0, 0, '5649', 'pull group 0'),
('zulfarrak', 6, 3, 'pull', 0, 1802.2, 738.7, 14.5, 30, 0, 0, 0, 0, '8095', 'pull group 1'),
('zulfarrak', 6, 4, 'move', 0, 1812.5333, 713.5999, 14.725753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 5, 'move', 0, 1813.3333, 703.99994, 15.225753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 6, 6, 'pull', 0, 1816.5, 673.5, 14.9, 15, 0, 0, 0, 0, '8130', 'pull group 2'),
('zulfarrak', 6, 7, 'boss', 8127, 1815.8, 670.4, 15, 0, 0, 0, 0, 0, '', 'boss'),
('zulfarrak', 7, 0, 'move', 0, 1814.3999, 679.19995, 14.425753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 7, 1, 'move', 0, 1813.3333, 703.99994, 15.225753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 7, 2, 'move', 0, 1812.5333, 713.5999, 14.725753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 7, 3, 'move', 0, 1804, 753.8666, 17.225752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 7, 4, 'move', 0, 1804, 762.66656, 17.625753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 7, 5, 'move', 0, 1807.2, 798.5, 13.9, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 7, 6, 'move', 0, 1808.2666, 810.66656, 12.525752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 7, 7, 'move', 0, 1812, 816.26654, 11.425753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 7, 8, 'move', 0, 1812, 819.1999, 10.525752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 7, 9, 'pull', 0, 1767.15, 853.55, 8.9, 30, 0, 0, 0, 0, '5649,7247', 'pull group 0'),
('zulfarrak', 7, 10, 'boss', 7272, 1778, 859.7, 8.9, 0, 0, 0, 0, 0, '', 'boss'),
('zulfarrak', 8, 0, 'move', 0, 1742.3999, 834.66656, 9.125752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 1, 'move', 0, 1704, 813.5999, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 2, 'move', 0, 1698.9333, 810.9332, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 3, 'move', 0, 1691.4667, 809.33325, 9.425753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 4, 'move', 0, 1682.4, 807.9999, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 5, 'move', 0, 1676.5333, 818.13324, 8.925753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 6, 'move', 0, 1674.9333, 821.5999, 9.025752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 7, 'move', 0, 1676.1, 833.5, 9.5, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 8, 8, 'move', 0, 1678.48, 856.47, 8.95, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 8, 9, 'move', 0, 1668.92, 872.47, 11.57, 0, 0, 0, 0, 0, '', 'via'),
('zulfarrak', 8, 10, 'move', 0, 1658.35, 878.91, 13.06, 0, 0, 0, 0, 0, '', 'to'),
('zulfarrak', 8, 11, 'pull', 0, 1666.77, 903.85, 8.97, 15, 0, 0, 0, 0, '5648,5650', 'pull group 0'),
('zulfarrak', 8, 12, 'pull', 0, 1660.45, 937.2, 9.15, 15, 0, 0, 0, 0, '7246,5648,5649', 'pull group 1'),
('zulfarrak', 8, 13, 'pull', 0, 1643.65, 904, 9.4, 15, 0, 0, 0, 0, '5648,7247', 'pull group 2 (patrol 7246: 45 points, 126 s round)'),
('zulfarrak', 8, 14, 'move', 0, 1647.7333, 915.7332, 10.425753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 15, 'move', 0, 1648.2666, 917.3332, 10.325752, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 16, 'move', 0, 1645.8667, 922.39984, 11.725753, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 17, 'pull', 0, 1605.77, 973.07, 9.13, 15, 0, 0, 0, 0, '5649', 'pull group 3'),
('zulfarrak', 8, 18, 'pull', 0, 1589.4, 985.4, 9.2, 30, 0, 0, 0, 0, '5648', 'pull group 4'),
('zulfarrak', 8, 19, 'pull', 0, 1585.1, 1035.5, 9, 30, 0, 0, 0, 0, '5649', 'pull group 5'),
('zulfarrak', 8, 20, 'pull', 0, 1583, 1086.76, 8.98, 30, 0, 0, 0, 0, '5650,7246,8095,5649', 'pull group 6'),
('zulfarrak', 8, 21, 'pull', 0, 1606.7, 1107.4, 9, 15, 0, 0, 0, 0, '7246,5650', 'pull group 7'),
('zulfarrak', 8, 22, 'pull', 0, 1639.44, 1106.82, 9.04, 15, 0, 0, 0, 0, '5649,5650,7246', 'pull group 8'),
('zulfarrak', 8, 23, 'pull', 0, 1649.33, 1157, 9.03, 30, 0, 0, 0, 0, '5649', 'pull group 9'),
('zulfarrak', 8, 24, 'move', 0, 1661.3334, 1161.0668, 9.633366, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 25, 'move', 0, 1685.3333, 1173.3334, 8.933366, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 26, 'pull', 0, 1715.48, 1186.18, 10.02, 15, 0, 0, 0, 0, '5649,5650,7246', 'pull group 10'),
('zulfarrak', 8, 27, 'pull', 0, 1714.4, 1218.6, 9, 30, 0, 0, 0, 0, '5649', 'pull group 11'),
('zulfarrak', 8, 28, 'move', 0, 1677.132, 1185.2784, 10.075073, 0, 0, 0, 0, 0, '', 'path'),
('zulfarrak', 8, 29, 'boss', 7795, 1650.4, 1161.4, 9.9, 0, 0, 0, 0, 0, '', 'boss'),
('zulfarrak', 9, 0, 'ritual', 141832, 1650.9, 1171.9, 10.9, 0, 0, 0, 0, 0, '', 'STOP: not supported. Gahz''rilla''s gong event (Mallet of Zul''Farrak) is not in the run leader; the live-proven plan ends at Velratha');
