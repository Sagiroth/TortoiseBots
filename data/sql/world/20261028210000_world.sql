-- Tower of Karazhan / Upper Karazhan Halls (map 814, the 40-man raid): the plan for the run leader (.bot run), exported
-- from turtle-agent prep/raid-tok e98f2dc (dungeons/upper_karazhan_halls.json, status 'draft (not mapped, not
-- live-checked)') with
--   turtle-agent plan-export --dir dungeons --mmaps <map 814 mmaps> --allow-draft --gm-cross-direct --approach-hops \
--     --rev e98f2dc --stamp 20261028210000 --out <this file> upper_karazhan_halls
-- NO ROW IS HAND-SET: the plan, leg and step rows below are the exporter's output; this header and the
-- ai_dungeon_boss block are the prep's own (task B1 of the raid master plan, ~/TortoiseBots-audit/RAID-PREP-tok.md).
--
-- What the prep changed from the dungeongen draft (8 legs, 76 pull groups, 204 units, no via or approach points, no
-- patrols; boss order a guess):
--   * 76 pull groups (204 units) become 56 (183 units): the draft's 15 neutral units (62023 Draenei Villager 6, 62024
--     Starving Draenei 5, 62025 Draenei Waterseeker 2, 62027 Na'va D'at, 62028 Kasat T'andr: faction 7, no hostile mask;
--     the map's 16th neutral 62029 Rud'an Malas was not in the draft; 61990/61997-62003 and 62030 are faction 35) are in
--     no pull; the 189 hostile units are chained where they stand within 15 yd on one floor (|dz| <= 4), never a boss, a
--     patrol or a linked boss add into trash, at most 16 units (a 40-man), every pair's walk at most 2.2 times their
--     distance plus 8 yd (353 candidate edges, none removed), the pull-reach closure joins every hostile within a pack's
--     radius + 3 (1 unit moved, 1 pack emptied). Radius = the pack's own spread + 0.5, capped by the nearest spawn of
--     every entry (the largest is 22.2). Rupturan's four Crumbling Exiles 59960 and Sanv's two Draenei Truthseekers 59980
--     are their bosses' `first` entries, not pulls.
--   * The route is the tower's spiral by Detour path length from the entrance (karaforty): 632 yd to Keeper Gnarlmoon,
--     1066 to Ley-Watcher Incantagos, 1545 to Anomalus, 1990 to Echo of Medivh, 2093 to the chess event. The draft put
--     Incantagos after the chess event and Anomalus before Incantagos; the guide's order and the path lengths agree on
--     Gnarlmoon, Incantagos, Anomalus, Medivh, chess, then the Outland bosses.
--   * Boss rows: the chess event's King 59967 carries Bishop 59971, Rook 59970 and Knight 59968 as `first` (the King is
--     invulnerable until they die, npc_kara_kingAI); Incantagos 61946 carries his four Ley Seekers 59989 (summoned at
--     spawn, each a 25 % damage reduction stack); Sanv 59981 his Truthseekers 59980 and Rupturan 59961 his Exiles 59960
--     (creature_group-linked adds). Mephistroth, the guide's ninth boss, has no creature_template, spawn row or script in
--     this core: eight bosses. Rupturan has a rank-3 spawn but no script and no EventAI (a dummy boss).
--   * Approach points: one `to` per leg, 27 packs (31 stands), 5 bosses (6 stands), 24 via points on the 954 yd cave walk
--     from Rupturan to Kruul.
--   * 11 patrol blocks (the map's waypoint patrols: 61933, 61936 x2, 61938, 61942 x2, 61943 x2, 61944, 61945, 61950)
--     are in the plan and NOT in the rows: the run leader pulls the pack where it stands.
--   * No door, lever, key or gossip gates the route (11 objects, all open door models, quest props and an anvil; no
--     instance script): no object, gossip or hold step.
--
-- plan-check (turtle-agent plan-check --mmaps <map 814 mmaps>): ok, 273 points on the navmesh, 0 failures, 3 exempt
-- (the gm_cross points), 11 warnings (the estimated patrol periods). The ROWS: 18 legs, 199 steps, 218 rows.
--
-- WHAT THE RUN LEADER CANNOT DO (nothing is dropped silently). Not in these rows:
--   * the Outland part (map region x -7200..-6000) is a separate navmesh island: the tower has no normal path to it
--     (the entrance to Sanv is 4427 yd of partial path). The plan starts its legs with `gm_cross` waypoints (the
--     karaoutland landing, Sanv's platform, Rupturan's approach); this export turns each into a `move_direct` row, and
--     the run leader cannot walk a 5008 yd gap or Sanv's 25.8 yd / 9 yd gap. Reaching Sanv, Rupturan and Kruul with the
--     module's run leader is an open blocker for the mapping walk and the live test (a GM move, or a live route found on
--     the mesh, is needed).
--   * the patrol paths and periods (11 groups): no column; the estimate (path length at 2.5 yd/s) is in the plan file.
--   * Keeper Gnarlmoon's red/blue sides, raven auras and owl intermission; Incantagos' five school affinities; Anomalus'
--     bombs and Arcane Prison; Echo of Medivh's adds; the chess pieces' roles: the bots' own combat AI is the only
--     handling, no tactic kind expresses them.
--   * the entrance (game_tele 'karaforty', no areatrigger row for map 814), level_band 60-60 and the raid composition
--     4/10/26: the run leader starts inside the instance and leads the bound party as it is.
--
-- ai_dungeon_boss (map 814): the eight boss steps' entries, names from the core's creature_template (all rank 3), so the
-- commander telemetry and the dashboard call them bosses.
-- ai_boss_tactic (map 814): none. The bosses cast from C++ (DoCastSpellIfCan), not creature_spells or EventAI (both empty
-- for them), and the world data has no cast time or interrupt flags (no Spell.dbc); no row qualifies as an interrupt, a
-- kill_first (the adds that heal or buff the boss are the boss rows' `first`) or a behind (no frontal cleave or breath in
-- the seven scripts).
--
-- Dungeon plans for the run leader (.bot run), exported by turtle-agent (plan-export). Plan format v1; the mapping is
-- docs/guides/dungeon-run-leader.md ("From plan v1 to rows"). The plan file in turtle-agent is the single source: edit it
-- there and export again, never these rows by hand. The plan row carries the unit lists cc_priority, cc_always and kill_first
-- (columns from 20261015200000_world.sql). Never edit this file once applied; export a new one.
--
-- Idempotent: each plan's rows are deleted, then inserted. The tables come from 20261014150000_world.sql.

-- upper_karazhan_halls: map 814, 18 legs, 199 steps
DELETE FROM `ai_dungeon_step` WHERE `plan` = 'upper_karazhan_halls';
DELETE FROM `ai_dungeon_leg` WHERE `plan` = 'upper_karazhan_halls';
DELETE FROM `ai_dungeon_plan` WHERE `plan` = 'upper_karazhan_halls';
INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, `kill_first`, `source`, `comment`) VALUES
('upper_karazhan_halls', 814, '', '', '', 'turtle-agent e98f2dc dungeons/upper_karazhan_halls.json', 'plan v1, draft (not mapped, not live-checked)');
INSERT INTO `ai_dungeon_leg` (`plan`, `leg`, `name`, `comment`) VALUES
('upper_karazhan_halls', 0, 'entrance_hall', ''),
('upper_karazhan_halls', 1, 'tower_ramp', ''),
('upper_karazhan_halls', 2, 'tower_hall', ''),
('upper_karazhan_halls', 3, 'keeper_gnarlmoon', ''),
('upper_karazhan_halls', 4, 'manascale_hall_west', ''),
('upper_karazhan_halls', 5, 'manascale_hall_east', ''),
('upper_karazhan_halls', 6, 'ley_watcher_incantagos', ''),
('upper_karazhan_halls', 7, 'arcane_hall', ''),
('upper_karazhan_halls', 8, 'anomalus', ''),
('upper_karazhan_halls', 9, 'upper_spire', ''),
('upper_karazhan_halls', 10, 'upper_spire_top', ''),
('upper_karazhan_halls', 11, 'echo_of_medivh', ''),
('upper_karazhan_halls', 12, 'king', ''),
('upper_karazhan_halls', 13, 'outland_landing', ''),
('upper_karazhan_halls', 14, 'outland_cave', ''),
('upper_karazhan_halls', 15, 'sanv_tas_dal', ''),
('upper_karazhan_halls', 16, 'rupturan_the_broken', ''),
('upper_karazhan_halls', 17, 'kruul', '');
INSERT INTO `ai_dungeon_step` (`plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, `timeout_s`, `entries`, `comment`) VALUES
('upper_karazhan_halls', 0, 0, 'move', 0, -11052.9, -2014.8, 98.86, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 0, 1, 'pull', 0, -11062.9, -2014.8, 96.97, 1, 0, 0, 0, 0, '61933', 'pull group 0'),
('upper_karazhan_halls', 0, 2, 'move', 0, -11036.1, -1980.4, 107.35, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('upper_karazhan_halls', 0, 3, 'pull', 0, -11023.1, -1957.88, 110.87, 1, 0, 0, 0, 0, '61934', 'pull group 1'),
('upper_karazhan_halls', 0, 4, 'move', 0, -11025.065, -1958.9333, 110.9984, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 0, 5, 'move', 0, -11025.866, -1959.4667, 111.29839, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 0, 6, 'move', 0, -11034.133, -1971.7333, 110.5984, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 0, 7, 'move', 0, -11054.666, -1988, 99.5984, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 0, 8, 'move', 0, -11057.065, -2000.2666, 96.89839, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 0, 9, 'move', 0, -11057.599, -2013.3333, 97.0984, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 0, 10, 'move', 0, -11057.6, -2018.35, 97.31, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('upper_karazhan_halls', 0, 11, 'pull', 0, -11067.6, -2018.35, 96.97, 1, 0, 0, 0, 0, '61935', 'pull group 2'),
('upper_karazhan_halls', 0, 12, 'move', 0, -11034.55, -1981.58, 107.78, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('upper_karazhan_halls', 0, 13, 'pull', 0, -11007.5, -1974.33, 110.72, 1, 0, 0, 0, 0, '61934', 'pull group 3'),
('upper_karazhan_halls', 0, 14, 'move', 0, -11016.8, -1941.93, 113.84, 0, 0, 0, 0, 0, '', 'pull group 4 approach'),
('upper_karazhan_halls', 0, 15, 'pull', 0, -11026.8, -1941.93, 113.78, 1, 0, 0, 0, 0, '61935', 'pull group 4'),
('upper_karazhan_halls', 0, 16, 'move', 0, -11050.14, -1962.41, 107.96, 0, 0, 0, 0, 0, '', 'pull group 5 approach'),
('upper_karazhan_halls', 0, 17, 'pull', 0, -11094.77, -1969.11, 92.39, 2.6, 0, 0, 0, 0, '61934', 'pull group 5'),
('upper_karazhan_halls', 1, 0, 'move', 0, -11059.8, -2001.83, 115.23, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 1, 1, 'pull', 0, -11055.5, -1999.12, 115.23, 6.1, 0, 0, 0, 0, '61937,61934', 'pull group 0'),
('upper_karazhan_halls', 1, 2, 'pull', 0, -11061.4, -2013.43, 115.22, 1, 0, 0, 0, 0, '61938', 'pull group 1'),
('upper_karazhan_halls', 1, 3, 'pull', 0, -11064.07, -2032.5, 115.17, 8.9, 0, 0, 0, 0, '61937,61936,61938', 'pull group 2'),
('upper_karazhan_halls', 1, 4, 'move', 0, -11034.61, -1995.92, 125.17, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('upper_karazhan_halls', 1, 5, 'pull', 0, -11034, -1974.9, 126.53, 1, 0, 0, 0, 0, '61936', 'pull group 3'),
('upper_karazhan_halls', 1, 6, 'pull', 0, -11007.6, -1979.75, 126.52, 1, 0, 0, 0, 0, '61934', 'pull group 4'),
('upper_karazhan_halls', 1, 7, 'move', 0, -11049.97, -1967.47, 124.68, 0, 0, 0, 0, 0, '', 'pull group 5 approach'),
('upper_karazhan_halls', 1, 8, 'pull', 0, -11068.44, -1959.82, 124.53, 15, 0, 0, 0, 0, '61938,61935,61932,61937,61933', 'pull group 5'),
('upper_karazhan_halls', 1, 9, 'move', 0, -11045.64, -1984.49, 129.53, 0, 0, 0, 0, 0, '', 'pull group 6 approach'),
('upper_karazhan_halls', 1, 10, 'pull', 0, -11054.2, -1989.39, 129.28, 1, 0, 0, 0, 0, '61934', 'pull group 6'),
('upper_karazhan_halls', 2, 0, 'move', 0, -11040.55, -1959.19, 143.1, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 2, 1, 'pull', 0, -11037.47, -1965.55, 143.08, 4.3, 0, 0, 0, 0, '61934,61933', 'pull group 0'),
('upper_karazhan_halls', 2, 2, 'pull', 0, -11077.7, -1954.68, 146.84, 1.9, 0, 0, 0, 0, '61937,61934', 'pull group 1'),
('upper_karazhan_halls', 2, 3, 'move', 0, -11040.27, -1976.61, 158.4, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('upper_karazhan_halls', 2, 4, 'pull', 0, -11047.34, -1983.68, 158.19, 6.4, 0, 0, 0, 0, '61938,61936,61937', 'pull group 2'),
('upper_karazhan_halls', 2, 5, 'pull', 0, -11035.3, -1947.73, 160.98, 1, 0, 0, 0, 0, '61937', 'pull group 3'),
('upper_karazhan_halls', 2, 6, 'move', 0, -11031.59, -1937.63, 161.46, 0, 0, 0, 0, 0, '', 'pull group 4 approach'),
('upper_karazhan_halls', 2, 7, 'pull', 0, -11030.4, -1914.97, 161.35, 1, 0, 0, 0, 0, '61937', 'pull group 4'),
('upper_karazhan_halls', 2, 8, 'pull', 0, -11038.3, -1902.14, 161.35, 1, 0, 0, 0, 0, '61936', 'pull group 5'),
('upper_karazhan_halls', 2, 9, 'pull', 0, -11004.4, -1909.21, 161.35, 4.4, 0, 0, 0, 0, '61936,61932', 'pull group 6'),
('upper_karazhan_halls', 3, 0, 'move', 0, -11008.1, -1874.38, 165.8, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 3, 1, 'pull', 0, -11018.1, -1874.38, 165.76, 1, 0, 0, 0, 0, '61936', 'pull group 0'),
('upper_karazhan_halls', 3, 2, 'pull', 0, -11025.9, -1855.28, 165.77, 1, 0, 0, 0, 0, '61936', 'pull group 1'),
('upper_karazhan_halls', 3, 3, 'move', 0, -11050.89, -1818.98, 165.8, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('upper_karazhan_halls', 3, 4, 'pull', 0, -11048.3, -1809.32, 165.77, 1, 0, 0, 0, 0, '61937', 'pull group 2'),
('upper_karazhan_halls', 3, 5, 'pull', 0, -11084.91, -1843.26, 165.77, 18.8, 0, 0, 0, 0, '61937,61938,61936', 'pull group 3'),
('upper_karazhan_halls', 3, 6, 'move', 0, -11080.4, -1845.98, 165.8, 0, 0, 0, 0, 0, '', 'boss approach'),
('upper_karazhan_halls', 3, 7, 'move', 0, -11113.63, -1868.24, 165.8, 0, 0, 0, 0, 0, '', 'boss approach'),
('upper_karazhan_halls', 3, 8, 'boss', 61939, -11145.2, -1889.39, 165.77, 0, 0, 0, 0, 0, '', 'boss'),
('upper_karazhan_halls', 4, 0, 'move', 0, -11167.2, -1860.5, 153.46, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 4, 1, 'pull', 0, -11176.5, -1859.96, 152.06, 1, 0, 0, 0, 0, '61944', 'pull group 0'),
('upper_karazhan_halls', 4, 2, 'move', 0, -11205.73, -1850.55, 152.22, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('upper_karazhan_halls', 4, 3, 'pull', 0, -11217.9, -1846.58, 152.05, 8.2, 0, 0, 0, 0, '61943,61944,61942', 'pull group 1'),
('upper_karazhan_halls', 4, 4, 'move', 0, -11230.53, -1873.04, 136.09, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('upper_karazhan_halls', 4, 5, 'pull', 0, -11240.53, -1873.04, 136.01, 10.3, 0, 0, 0, 0, '59988', 'pull group 2'),
('upper_karazhan_halls', 4, 6, 'pull', 0, -11273.87, -1870.51, 135.98, 4.7, 0, 0, 0, 0, '59988,61944', 'pull group 3'),
('upper_karazhan_halls', 4, 7, 'pull', 0, -11272.2, -1860.45, 135.83, 1, 0, 0, 0, 0, '61943', 'pull group 4'),
('upper_karazhan_halls', 4, 8, 'move', 0, -11260.69, -1846.76, 135.92, 0, 0, 0, 0, 0, '', 'pull group 5 approach'),
('upper_karazhan_halls', 4, 9, 'pull', 0, -11270.29, -1838.4, 135.72, 10.9, 0, 0, 0, 0, '59988,61944,61942', 'pull group 5'),
('upper_karazhan_halls', 4, 10, 'pull', 0, -11262.8, -1814.55, 135.62, 1, 0, 0, 0, 0, '61943', 'pull group 6'),
('upper_karazhan_halls', 5, 0, 'move', 0, -11227.94, -1797.37, 135.98, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 5, 1, 'pull', 0, -11227.94, -1807.37, 136, 15.5, 0, 0, 0, 0, '59988,61940', 'pull group 0'),
('upper_karazhan_halls', 5, 2, 'move', 0, -11196.51, -1804.33, 136.1, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('upper_karazhan_halls', 5, 3, 'pull', 0, -11187.27, -1808.15, 136.02, 4.4, 0, 0, 0, 0, '61944', 'pull group 1'),
('upper_karazhan_halls', 5, 4, 'pull', 0, -11177.36, -1841.98, 136.02, 9.4, 0, 0, 0, 0, '59988', 'pull group 2'),
('upper_karazhan_halls', 5, 5, 'pull', 0, -11189.9, -1800.56, 136.02, 1, 0, 0, 0, 0, '61942', 'pull group 3'),
('upper_karazhan_halls', 5, 6, 'pull', 0, -11217.55, -1768.52, 135.99, 3.1, 0, 0, 0, 0, '59988', 'pull group 4'),
('upper_karazhan_halls', 5, 7, 'move', 0, -11214.79, -1768.75, 136.08, 0, 0, 0, 0, 0, '', 'pull group 5 approach'),
('upper_karazhan_halls', 5, 8, 'pull', 0, -11227.87, -1741.71, 136.01, 3.3, 0, 0, 0, 0, '59988', 'pull group 5'),
('upper_karazhan_halls', 5, 9, 'pull', 0, -11199.3, -1740.59, 136.02, 8.3, 0, 0, 0, 0, '61942,59988,61943,61944', 'pull group 6'),
('upper_karazhan_halls', 5, 10, 'move', 0, -11251.44, -1752.72, 136, 0, 0, 0, 0, 0, '', 'pull group 7 approach'),
('upper_karazhan_halls', 5, 11, 'pull', 0, -11281.08, -1739.75, 135.98, 22.2, 0, 0, 0, 0, '59988,61942,61943,61944', 'pull group 7'),
('upper_karazhan_halls', 5, 12, 'move', 0, -11270.77, -1786.7, 141.2, 0, 0, 0, 0, 0, '', 'pull group 8 approach'),
('upper_karazhan_halls', 5, 13, 'pull', 0, -11278, -1803.84, 147.19, 1, 0, 0, 0, 0, '59988', 'pull group 8'),
('upper_karazhan_halls', 5, 14, 'move', 0, -11287.77, -1761.66, 135.81, 0, 0, 0, 0, 0, '', 'pull group 9 approach'),
('upper_karazhan_halls', 5, 15, 'pull', 0, -11291.7, -1734, 136.04, 1, 0, 0, 0, 0, '61945', 'pull group 9'),
('upper_karazhan_halls', 6, 0, 'move', 0, -11284, -1771.7334, 135.68974, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 6, 1, 'move', 0, -11284, -1785.8667, 141.88974, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 6, 2, 'move', 0, -11285.333, -1792, 144.48973, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 6, 3, 'move', 0, -11303.595, -1836.475, 158.21835, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 6, 4, 'move', 0, -11306.8, -1844.28, 160.2, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 6, 5, 'pull', 0, -11305.97, -1836.61, 159.09, 8.3, 0, 0, 0, 0, '59988', 'pull group 0'),
('upper_karazhan_halls', 6, 6, 'move', 0, -11328.3, -1859.53, 166.11, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('upper_karazhan_halls', 6, 7, 'pull', 0, -11338.3, -1859.53, 167.91, 7.7, 0, 0, 0, 0, '59988', 'pull group 1'),
('upper_karazhan_halls', 6, 8, 'move', 0, -11361.95, -1839.78, 174.06, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('upper_karazhan_halls', 6, 9, 'pull', 0, -11393.64, -1823.26, 179.7, 10.9, 0, 0, 0, 0, '61944,61942,61943,61940', 'pull group 2'),
('upper_karazhan_halls', 6, 10, 'move', 0, -11354.52, -1790.99, 179.79, 0, 0, 0, 0, 0, '', 'boss approach'),
('upper_karazhan_halls', 6, 11, 'boss', 61946, -11334.2, -1782.57, 179.73, 0, 0, 0, 0, 0, '59989', 'boss'),
('upper_karazhan_halls', 7, 0, 'move', 0, -11296.066, -1752.927, 192.71811, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 7, 1, 'move', 0, -11264, -1728, 203.58974, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 7, 2, 'move', 0, -11222.866, -1700.4991, 210.78107, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 7, 3, 'move', 0, -11222, -1699.92, 210.88, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 7, 4, 'pull', 0, -11228.53, -1699.47, 210.88, 13.4, 0, 0, 0, 0, '59988,61944', 'pull group 0'),
('upper_karazhan_halls', 7, 5, 'pull', 0, -11215.1, -1717.17, 210.96, 1, 0, 0, 0, 0, '61942', 'pull group 1'),
('upper_karazhan_halls', 7, 6, 'move', 0, -11199.8, -1732.92, 207.11, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('upper_karazhan_halls', 7, 7, 'move', 0, -11177.04, -1765.52, 201.34, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('upper_karazhan_halls', 7, 8, 'pull', 0, -11169.79, -1775.9, 201.24, 13.1, 0, 0, 0, 0, '59988,61943', 'pull group 2'),
('upper_karazhan_halls', 8, 0, 'move', 0, -11182.666, -1785.0667, 201.7984, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 8, 1, 'move', 0, -11200, -1792, 205.88974, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 8, 2, 'move', 0, -11221.333, -1813.3334, 223.98973, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 8, 3, 'move', 0, -11229.8, -1830.41, 223.94, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 8, 4, 'pull', 0, -11232.5, -1833.79, 223.94, 11.9, 0, 0, 0, 0, '61947,61943,61948', 'pull group 0'),
('upper_karazhan_halls', 8, 5, 'move', 0, -11195.59, -1867.24, 231.53, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('upper_karazhan_halls', 8, 6, 'pull', 0, -11193, -1876.9, 231.95, 1, 0, 0, 0, 0, '61944', 'pull group 1'),
('upper_karazhan_halls', 8, 7, 'move', 0, -11228.17, -1847.62, 223.99, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('upper_karazhan_halls', 8, 8, 'move', 0, -11257.44, -1865.48, 235.04, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('upper_karazhan_halls', 8, 9, 'pull', 0, -11256.7, -1896.99, 242.27, 1, 0, 0, 0, 0, '61947', 'pull group 2'),
('upper_karazhan_halls', 8, 10, 'move', 0, -11254.59, -1894.74, 242.37, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('upper_karazhan_halls', 8, 11, 'pull', 0, -11243.4, -1913.51, 242.27, 1, 0, 0, 0, 0, '61950', 'pull group 3'),
('upper_karazhan_halls', 8, 12, 'move', 0, -11264.56, -1929.63, 240.49, 0, 0, 0, 0, 0, '', 'pull group 4 approach'),
('upper_karazhan_halls', 8, 13, 'pull', 0, -11280.98, -1950.55, 240.47, 11.3, 0, 0, 0, 0, '61947,61948,61949', 'pull group 4'),
('upper_karazhan_halls', 8, 14, 'move', 0, -11299.12, -1909.5, 240.49, 0, 0, 0, 0, 0, '', 'boss approach'),
('upper_karazhan_halls', 8, 15, 'boss', 61951, -11318.7, -1898.1, 240.47, 0, 0, 0, 0, 0, '', 'boss'),
('upper_karazhan_halls', 9, 0, 'move', 0, -11333.333, -1890.9333, 242.28973, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 9, 1, 'move', 0, -11349.333, -1877.3334, 244.08974, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 9, 2, 'move', 0, -11370.666, -1856, 260.68973, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 9, 3, 'move', 0, -11393.7, -1827.4, 260.83, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 9, 4, 'pull', 0, -11395.24, -1824.22, 260.86, 7, 0, 0, 0, 0, '61948,61947,61950', 'pull group 0'),
('upper_karazhan_halls', 10, 0, 'move', 0, -11364.267, -1801.0667, 266.98975, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 10, 1, 'move', 0, -11349.333, -1792, 265.18973, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 10, 2, 'move', 0, -11311.062, -1761.6548, 275.88983, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 10, 3, 'move', 0, -11272.791, -1731.3094, 286.58994, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 10, 4, 'move', 0, -11259.2, -1720.5333, 290.38974, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 10, 5, 'move', 0, -11238.7, -1706.81, 290.35, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 10, 6, 'pull', 0, -11230.68, -1700.5, 290.35, 10.7, 0, 0, 0, 0, '61957,61955,61956,61954', 'pull group 0'),
('upper_karazhan_halls', 11, 0, 'move', 0, -11192.49, -1668.5677, 285.71432, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 11, 1, 'move', 0, -11154.3, -1636.6355, 281.03888, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 11, 2, 'move', 0, -11135.999, -1621.3334, 278.7984, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 11, 3, 'move', 0, -11106.8, -1602.01, 279.95, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 11, 4, 'move', 0, -11096.8, -1602.01, 280.02, 0, 0, 0, 0, 0, '', 'boss approach'),
('upper_karazhan_halls', 11, 5, 'boss', 61958, -11106.8, -1602.01, 279.95, 0, 0, 0, 0, 0, '', 'boss'),
('upper_karazhan_halls', 12, 0, 'move', 0, -11145.251, -1633.7324, 283.62897, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 1, 'move', 0, -11183.703, -1665.455, 287.5236, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 2, 'move', 0, -11212, -1688.8, 290.38974, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 3, 'move', 0, -11221.333, -1706.6667, 290.38974, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 4, 'move', 0, -11259.2, -1720.5333, 290.38974, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 5, 'move', 0, -11275.467, -1733.6, 277.38974, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 6, 'move', 0, -11285.333, -1741.6, 267.88974, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 7, 'move', 0, -11287.733, -1743.7334, 265.78973, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 8, 'move', 0, -11287.467, -1744.8, 265.68973, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 9, 'move', 0, -11285.333, -1748, 265.18973, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 10, 'move', 0, -11283.733, -1748.2667, 265.08975, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 11, 'move', 0, -11244.797, -1726.7761, 242.24022, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 12, 'move', 0, -11242.666, -1725.6, 240.98973, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 13, 'move', 0, -11229.866, -1722.4, 240.98973, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 14, 'move', 0, -11225.6, -1721.6, 240.98973, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 15, 'move', 0, -11224.533, -1721.6, 240.98973, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 16, 'move', 0, -11190.776, -1757.7114, 248.50296, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 17, 'move', 0, -11178.666, -1770.6667, 251.1984, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 18, 'move', 0, -11142.362, -1803.3905, 240.65263, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 19, 'move', 0, -11140.799, -1804.8, 240.1984, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 20, 'move', 0, -11114.666, -1834.6666, 233.9984, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 21, 'move', 0, -11112.266, -1845.3334, 228.5984, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 22, 'move', 0, -11122.133, -1858.4, 223.6984, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 23, 'move', 0, -11122.399, -1859.4667, 224.5984, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 24, 'move', 0, -11119.199, -1862.9333, 222.9984, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 25, 'move', 0, -11077.569, -1890.5953, 221.6623, 0, 0, 0, 0, 0, '', 'path'),
('upper_karazhan_halls', 12, 26, 'move', 0, -11060, -1902.27, 221.07, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 12, 27, 'move', 0, -11051.34, -1897.27, 221.1, 0, 0, 0, 0, 0, '', 'boss approach'),
('upper_karazhan_halls', 12, 28, 'boss', 59967, -11060, -1902.27, 221.07, 0, 0, 0, 0, 0, '59971,59970,59968', 'boss'),
('upper_karazhan_halls', 13, 0, 'move_direct', 0, -6078.86, -2395.53, 49.25, 0, 0, 0, 0, 0, '', 'via (gm_cross in the plan; move_direct, not tried live)'),
('upper_karazhan_halls', 13, 1, 'move', 0, -6078.86, -2395.53, 49.25, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 13, 2, 'pull', 0, -6064.66, -2394.94, 49.54, 1, 0, 0, 0, 0, '62022', 'pull group 0'),
('upper_karazhan_halls', 14, 0, 'move', 0, -6064.66, -2394.94, 49.54, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 14, 1, 'move', 0, -6065.76, -2409.97, 46.06, 0, 0, 0, 0, 0, '', 'pull group 0 approach'),
('upper_karazhan_halls', 14, 2, 'move', 0, -6066.59, -2446.09, 28.96, 0, 0, 0, 0, 0, '', 'pull group 0 approach'),
('upper_karazhan_halls', 14, 3, 'move', 0, -6088.91, -2473.46, 10.91, 0, 0, 0, 0, 0, '', 'pull group 0 approach'),
('upper_karazhan_halls', 14, 4, 'pull', 0, -6100.22, -2500.25, 13.36, 1, 0, 0, 0, 0, '62020', 'pull group 0'),
('upper_karazhan_halls', 15, 0, 'move_direct', 0, -6440, -2720, 21.9, 0, 0, 0, 0, 0, '', 'via (gm_cross in the plan; move_direct, not tried live)'),
('upper_karazhan_halls', 15, 1, 'move', 0, -6440, -2720, 21.9, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 15, 2, 'boss', 59981, -6451.91, -2731.28, 22.13, 0, 0, 0, 0, 0, '59980', 'boss'),
('upper_karazhan_halls', 16, 0, 'move_direct', 0, -6265, -2845, -35.37, 0, 0, 0, 0, 0, '', 'via (gm_cross in the plan; move_direct, not tried live)'),
('upper_karazhan_halls', 16, 1, 'move', 0, -6265, -2845, -35.37, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 16, 2, 'boss', 59961, -6248.46, -2863.91, -37.67, 0, 0, 0, 0, 0, '59960', 'boss'),
('upper_karazhan_halls', 17, 0, 'move', 0, -6303.55, -2855.61, -36.84, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 1, 'move', 0, -6324.34, -2862.91, -25.9, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 2, 'move', 0, -6361.67, -2867.58, -12.79, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 3, 'move', 0, -6398.89, -2872.23, 0.67, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 4, 'move', 0, -6437.13, -2878.89, 9.01, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 5, 'move', 0, -6474.93, -2889.33, 12.84, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 6, 'move', 0, -6513.85, -2898.13, 9, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 7, 'move', 0, -6550.8, -2912.73, 9.06, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 8, 'move', 0, -6588.29, -2920.33, 9.01, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 9, 'move', 0, -6627.93, -2915.07, 9.25, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 10, 'move', 0, -6667.54, -2909.81, 9.12, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 11, 'move', 0, -6695.01, -2908.29, 11.06, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 12, 'move', 0, -6735, -2908.39, 9.62, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 13, 'move', 0, -6770.93, -2908.49, 9.04, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 14, 'move', 0, -6810.46, -2908.9, 9.4, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 15, 'move', 0, -6850.45, -2909.56, 10, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 16, 'move', 0, -6890.44, -2910.22, 10.57, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 17, 'move', 0, -6926.41, -2910.82, 12.1, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 18, 'move', 0, -6966.25, -2911.48, 7.44, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 19, 'move', 0, -6994.24, -2911.51, 9.11, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 20, 'move', 0, -7034.23, -2910.78, 11.92, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 21, 'move', 0, -7074.22, -2910.04, 10.38, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 22, 'move', 0, -7114.21, -2909.31, 12.23, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 23, 'move', 0, -7151.95, -2898.51, 12.65, 0, 0, 0, 0, 0, '', 'via'),
('upper_karazhan_halls', 17, 24, 'move', 0, -7179.86, -2878.26, 13.3, 0, 0, 0, 0, 0, '', 'to'),
('upper_karazhan_halls', 17, 25, 'boss', 59991, -7189.86, -2878.26, 13.23, 0, 0, 0, 0, 0, '', 'boss');

-- The eight bosses of the plan's boss steps (map 814), for the boss rank and the dashboard: a DELETE of this map and
-- these entries, then REPLACE.
DELETE FROM `ai_dungeon_boss` WHERE `map` IN (814) AND `entry` IN (61939, 61946, 61951, 61958, 59967, 59981, 59961, 59991);
REPLACE INTO `ai_dungeon_boss` (`map`, `entry`, `name`) VALUES
(814, 61939, 'Keeper Gnarlmoon'),
(814, 61946, 'Ley-Watcher Incantagos'),
(814, 61951, 'Anomalus'),
(814, 61958, 'Echo of Medivh'),
(814, 59967, 'King'),
(814, 59981, 'Sanv Tas''dal'),
(814, 59961, 'Rupturan the Broken'),
(814, 59991, 'Kruul');
