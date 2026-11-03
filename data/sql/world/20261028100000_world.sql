-- Lower Karazhan Halls (map 532, the 10-man raid): the plan for the run leader (.bot run), exported from turtle-agent
-- prep/raid-lkh 954d1af (dungeons/lower_karazhan_halls.json, status 'draft (not mapped, not live-checked)') with
--   turtle-agent plan-export --dir dungeons --mmaps <map 532 mmaps> --allow-draft --gm-cross-direct --approach-hops \
--     --rev 954d1af --stamp 20261028100000 --out <this file> lower_karazhan_halls
-- NO ROW IS HAND-SET: the plan, leg and step rows below are the exporter's output; this header, the ai_dungeon_boss
-- block and the ai_boss_tactic block are the prep's own (task B1, ~/TortoiseBots-audit/RAID-PREP-lkh.md).
--
-- What the prep changed from the dungeongen draft (4 legs, 137 pull groups, no via or approach points):
--   * 137 pull groups (392 units) become 93 (292 units): the runts (4075 Rat 38, 14881 Spider 61: level 1, rank 0,
--     faction 28 'Prey' and 148 'Ambient') are in no pull; the hostiles are chained where they stand within 15 yd on
--     one floor (|dz| <= 4), every hostile spawn within the pull's reach is pulled into the pack up to the 8-unit cap,
--     and a pack whose members stand in two rooms (a wall between, the walk 2.2 times their distance + 8 yd or worse)
--     is split. Radius = the pack's own spread from its centre + 0.5, at most 35 (the largest here is 18). 14261 Blue
--     Drakonid has no normal path to any other plan point on the map 532 mesh and is in no pull.
--   * Lord Blackwald II (61222) is a boss leg: the instance script summons him at -11088.2 -1995.74 76.1774 when the
--     Dark Rider Champion (61204) is DONE. Moroes (61225) is last and is talked to first (gossip, menu index 0, row
--     `cmd gossip 61225 <lead bot> 1`): his menu offers it only when the other four bosses are DONE.
--   * Grizikil (61224) is pulled after every Grellkin pack within 190 yd: his Aggro puts them into combat with the zone.
--   * 43 approach stands (40 packs, 3 bosses) on the packs' own floors and one `to` per leg, so the run leader does
--     not walk toward a pack it cannot see in hops of its own.
--   * 10 patrol blocks (the map's waypoint patrols) are in the plan and NOT in the rows: the run leader pulls the pack
--     where it stands.
--   * The map's doors are all open (state 1) and no lever or key gates the route: no object step. The chests, the anvil
--     and the two quest objects (Strange Marble Bust 2020051, The Orb of Pyforos 2020052) are left alone.
--
-- plan-check (turtle-agent plan-check --mmaps <map 532 mmaps>): ok, 353 points on the navmesh, 0 failures, 10
-- warnings, all "the patrol of ... has only an estimated period_s": the mapping walk (B2) measures the rounds.
-- The ROWS: 13 legs, 257 steps (158 move, 93 pull, 5 boss, 1 gossip), 271 rows with the plan and leg rows.
--
-- WHAT THE RUN LEADER CANNOT DO (nothing is dropped silently). Not in these rows:
--   * the patrol paths and periods (10 groups): no column; the run leader pulls the pack where it stands when it
--     comes. The estimate (path length at 2.5 yd/s) is in the plan file.
--   * Moroes' tank swap: Glittering Dust (57095) drops the tank's threat 100 % and moves him to the second on the
--     threat list (boss_moroes.cpp TryCastGlitteringDust); no tactic kind expresses it. The bots' own raid strategy
--     (LowerKarazhanDungeonStrategies.cpp, AiPlayerbot.EnableCustomRaidTactics) is the only handling.
--   * Araxxna's eggs: skitterweb_eggAI kills an egg after 20 s and hatches a random spider in combat; killing eggs
--     early is wasted damage and the plan does not.
--   * the entrance (trigger 5018, portal at -11110.5 -2004.25 49.331 on map 0), level_band 59-60 and the raid
--     composition 2/3/5: the run leader starts inside the instance and leads the bound party as it is.
--
-- ai_dungeon_boss (map 532): the five boss steps' entries, names from the core's creature_template (all rank 3), so
-- the commander telemetry and the dashboard call them bosses. Blackwald II 61222 has no creature spawn row (the
-- instance summons him); his row is here for the plan's boss step.
-- ai_boss_tactic (map 532): Grizikil's Grellkin Fireball 57094 (boss_grizikil.cpp, DoCastSpellIfCan on the victim
-- every 4-6 s); the cast time is not in the world data (no Spell.dbc in the prep's data set), so the row is written
-- from the script and the spell name. Moroes' Shadow Blast 57099 is not repeated: the bots' Moroes fight strategy
-- already interrupts it (LowerKarazhanDungeonStrategies.cpp).
--
-- Dungeon plans for the run leader (.bot run), exported by turtle-agent (plan-export). Plan format v1; the mapping is
-- docs/guides/dungeon-run-leader.md ("From plan v1 to rows"). The plan file in turtle-agent is the single source: edit it
-- there and export again, never these rows by hand. The plan row carries the unit lists cc_priority, cc_always and kill_first
-- (columns from 20261015200000_world.sql). Never edit this file once applied; export a new one.
--
-- Idempotent: each plan's rows are deleted, then inserted. The tables come from 20261014150000_world.sql.

-- lower_karazhan_halls: map 532, 13 legs, 257 steps
DELETE FROM `ai_dungeon_step` WHERE `plan` = 'lower_karazhan_halls';
DELETE FROM `ai_dungeon_leg` WHERE `plan` = 'lower_karazhan_halls';
DELETE FROM `ai_dungeon_plan` WHERE `plan` = 'lower_karazhan_halls';
INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, `kill_first`, `source`, `comment`) VALUES
('lower_karazhan_halls', 532, '', '', '', 'turtle-agent 954d1af dungeons/lower_karazhan_halls.json', 'plan v1, draft (not mapped, not live-checked)');
INSERT INTO `ai_dungeon_leg` (`plan`, `leg`, `name`, `comment`) VALUES
('lower_karazhan_halls', 0, 'entrance_hall_ring', ''),
('lower_karazhan_halls', 1, 'entrance_hall_west', ''),
('lower_karazhan_halls', 2, 'entrance_hall_dining', ''),
('lower_karazhan_halls', 3, 'entrance_hall_east', ''),
('lower_karazhan_halls', 4, 'brood_queen_araxxna', ''),
('lower_karazhan_halls', 5, 'spider_wing', ''),
('lower_karazhan_halls', 6, 'north_bridge', ''),
('lower_karazhan_halls', 7, 'north_bridge_east', ''),
('lower_karazhan_halls', 8, 'grizikil', ''),
('lower_karazhan_halls', 9, 'clawlord_howlfang', ''),
('lower_karazhan_halls', 10, 'karazhan_terrace', ''),
('lower_karazhan_halls', 11, 'moroes_wing', ''),
('lower_karazhan_halls', 12, 'moroes', '');
INSERT INTO `ai_dungeon_step` (`plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, `timeout_s`, `entries`, `comment`) VALUES
('lower_karazhan_halls', 0, 0, 'move', 0, -11064.62, -1977.33, 54.1, 0, 0, 0, 0, 0, '', 'to'),
('lower_karazhan_halls', 0, 1, 'pull', 0, -11044.5, -1968.89, 53.7, 4, 0, 0, 0, 0, '61200', 'pull group 0'),
('lower_karazhan_halls', 0, 2, 'move', 0, -11012.532, -1964.2666, 66.89839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 0, 3, 'move', 0, -11007.999, -1962.6666, 68.49839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 0, 4, 'move', 0, -10990.133, -1961.8666, 80.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 0, 5, 'move', 0, -10989.065, -1960, 79.9984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 0, 6, 'move', 0, -10979.999, -1956, 80.698395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 0, 7, 'move', 0, -10973.332, -1954.1333, 79.9984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 0, 8, 'move', 0, -10972.799, -1952, 79.9984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 0, 9, 'move', 0, -10971.732, -1947.7333, 78.89839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 0, 10, 'move', 0, -10969.49, -1936, 79.16, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('lower_karazhan_halls', 0, 11, 'pull', 0, -10989.9, -1941.91, 78.87, 1, 0, 0, 0, 0, '61192', 'pull group 1'),
('lower_karazhan_halls', 0, 12, 'pull', 0, -10962.2, -1935.4, 78.87, 6.7, 0, 0, 0, 0, '61194,61192', 'pull group 2'),
('lower_karazhan_halls', 0, 13, 'pull', 0, -10955.03, -1894.45, 78.87, 10.5, 0, 0, 0, 0, '61192,61193,61194', 'pull group 3'),
('lower_karazhan_halls', 0, 14, 'pull', 0, -10985.5, -1903.7, 78.88, 1, 0, 0, 0, 0, '61194', 'pull group 4'),
('lower_karazhan_halls', 0, 15, 'move', 0, -10988, -1895.56, 80.03, 0, 0, 0, 0, 0, '', 'pull group 5 approach'),
('lower_karazhan_halls', 0, 16, 'pull', 0, -11014.95, -1887.04, 78.87, 18, 0, 0, 0, 0, '61192,61194,61193,61191', 'pull group 5'),
('lower_karazhan_halls', 0, 17, 'pull', 0, -11009.13, -1931.09, 78.87, 3.9, 0, 0, 0, 0, '61193,61194,61192', 'pull group 6'),
('lower_karazhan_halls', 0, 18, 'move', 0, -11038.93, -1941.33, 77.3, 0, 0, 0, 0, 0, '', 'pull group 7 approach'),
('lower_karazhan_halls', 0, 19, 'pull', 0, -11048.1, -1927.62, 77.27, 6.3, 0, 0, 0, 0, '61211', 'pull group 7'),
('lower_karazhan_halls', 0, 20, 'pull', 0, -11065.1, -1917.71, 77.27, 1, 0, 0, 0, 0, '61210', 'pull group 8'),
('lower_karazhan_halls', 0, 21, 'pull', 0, -11062.52, -1958.59, 77.35, 12.9, 0, 0, 0, 0, '61211,61210', 'pull group 9'),
('lower_karazhan_halls', 0, 22, 'pull', 0, -11049.47, -1984.01, 77.27, 1.8, 0, 0, 0, 0, '61210', 'pull group 10'),
('lower_karazhan_halls', 0, 23, 'move', 0, -11046.13, -1977.07, 77.3, 0, 0, 0, 0, 0, '', 'pull group 11 approach'),
('lower_karazhan_halls', 0, 24, 'pull', 0, -11052.2, -2007.25, 77.27, 7.2, 0, 0, 0, 0, '61211,61210', 'pull group 11'),
('lower_karazhan_halls', 0, 25, 'pull', 0, -11073.55, -1993.58, 77.27, 2.2, 0, 0, 0, 0, '61210', 'pull group 12'),
('lower_karazhan_halls', 0, 26, 'move', 0, -11094.47, -1950.07, 74.42, 0, 0, 0, 0, 0, '', 'pull group 13 approach'),
('lower_karazhan_halls', 0, 27, 'pull', 0, -11099.32, -1923.83, 74.42, 17, 0, 0, 0, 0, '61211,61200,61198,61202', 'pull group 13'),
('lower_karazhan_halls', 0, 28, 'move', 0, -11090.24, -1906.99, 74.42, 0, 0, 0, 0, 0, '', 'pull group 14 approach'),
('lower_karazhan_halls', 0, 29, 'pull', 0, -11103.67, -1888.81, 74.4, 5.2, 0, 0, 0, 0, '61198,61200', 'pull group 14'),
('lower_karazhan_halls', 0, 30, 'move', 0, -11129.13, -1892, 74.5, 0, 0, 0, 0, 0, '', 'pull group 15 approach'),
('lower_karazhan_halls', 0, 31, 'pull', 0, -11148.9, -1903.24, 74.4, 1, 0, 0, 0, 0, '61204', 'pull group 15'),
('lower_karazhan_halls', 0, 32, 'move', 0, -11150, -1933.78, 74.5, 0, 0, 0, 0, 0, '', 'pull group 16 approach'),
('lower_karazhan_halls', 0, 33, 'pull', 0, -11130.15, -1949.79, 74.4, 3.3, 0, 0, 0, 0, '61200,61198,61202', 'pull group 16'),
('lower_karazhan_halls', 0, 34, 'pull', 0, -11115.77, -1964.82, 74.39, 2.6, 0, 0, 0, 0, '61202,61200', 'pull group 17'),
('lower_karazhan_halls', 0, 35, 'pull', 0, -11149.2, -1961.57, 74.39, 1, 0, 0, 0, 0, '61200', 'pull group 18'),
('lower_karazhan_halls', 0, 36, 'pull', 0, -11164.35, -1942.4, 74.4, 2.7, 0, 0, 0, 0, '61200,61198', 'pull group 19'),
('lower_karazhan_halls', 0, 37, 'move', 0, -11127.732, -1956.5333, 74.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 0, 38, 'move', 0, -11106.933, -1962.6666, 74.89839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 0, 39, 'move', 0, -11099.07, -1972, 76.2, 0, 0, 0, 0, 0, '', 'boss approach'),
('lower_karazhan_halls', 0, 40, 'boss', 61222, -11088.2, -1995.74, 76.18, 0, 0, 0, 0, 0, '', 'boss'),
('lower_karazhan_halls', 1, 0, 'move', 0, -11093.332, -1984, 76.198395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 1, 'move', 0, -11103.732, -1966.6666, 76.198395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 2, 'move', 0, -11106.933, -1962.6666, 74.89839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 3, 'move', 0, -11108.266, -1962.6666, 74.79839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 4, 'move', 0, -11114.666, -1962.6666, 74.39839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 5, 'move', 0, -11127.732, -1956.5333, 74.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 6, 'move', 0, -11156.532, -1926.1333, 74.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 7, 'move', 0, -11157.332, -1920, 74.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 8, 'move', 0, -11157.332, -1898.6666, 74.39839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 9, 'move', 0, -11146.933, -1892.2666, 70.79839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 10, 'move', 0, -11139.466, -1888, 66.5984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 11, 'move', 0, -11128.532, -1886.1333, 60.898396, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 12, 'move', 0, -11124.799, -1886.1333, 59.198395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 13, 'move', 0, -11115.466, -1887.4667, 53.998394, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 1, 14, 'move', 0, -11106.73, -1891.27, 50.15, 0, 0, 0, 0, 0, '', 'to'),
('lower_karazhan_halls', 1, 15, 'pull', 0, -11123.3, -1893.91, 49.89, 2.4, 0, 0, 0, 0, '61199,61198', 'pull group 0'),
('lower_karazhan_halls', 2, 0, 'move', 0, -11096, -1903.2, 50.9, 0, 0, 0, 0, 0, '', 'to'),
('lower_karazhan_halls', 2, 1, 'pull', 0, -11096.7, -1908.05, 49.88, 9.7, 0, 0, 0, 0, '61200,61202', 'pull group 0'),
('lower_karazhan_halls', 2, 2, 'pull', 0, -11112.2, -1918.08, 49.51, 3.5, 0, 0, 0, 0, '61200', 'pull group 1'),
('lower_karazhan_halls', 3, 0, 'move', 0, -11111.04, -1937.49, 51.04, 0, 0, 0, 0, 0, '', 'to'),
('lower_karazhan_halls', 3, 1, 'pull', 0, -11097.7, -1929.86, 49.89, 1, 0, 0, 0, 0, '61201', 'pull group 0'),
('lower_karazhan_halls', 3, 2, 'pull', 0, -11101.9, -1931.55, 49.89, 1, 0, 0, 0, 0, '61201', 'pull group 1'),
('lower_karazhan_halls', 4, 0, 'move', 0, -11116, -1908.27, 49.9, 0, 0, 0, 0, 0, '', 'to'),
('lower_karazhan_halls', 4, 1, 'pull', 0, -11143.1, -1897.52, 49.89, 5.5, 0, 0, 0, 0, '61198,61200,61201', 'pull group 0'),
('lower_karazhan_halls', 4, 2, 'pull', 0, -11147.05, -1932.32, 49.75, 11.3, 0, 0, 0, 0, '61201,61319,61198', 'pull group 1'),
('lower_karazhan_halls', 4, 3, 'move', 0, -11147.64, -1930.58, 51.17, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('lower_karazhan_halls', 4, 4, 'pull', 0, -11128.2, -1957.64, 49.89, 11.3, 0, 0, 0, 0, '61200,61201', 'pull group 2'),
('lower_karazhan_halls', 4, 5, 'move', 0, -11098.399, -1948, 49.898396, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 4, 6, 'move', 0, -11097.599, -1948, 49.898396, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 4, 7, 'move', 0, -11095.199, -1949.3333, 49.898396, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 4, 8, 'move', 0, -11093.332, -1950.4, 49.898396, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 4, 9, 'move', 0, -11091.732, -1952.2666, 49.898396, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 4, 10, 'move', 0, -11085.599, -1960.2666, 49.698395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 4, 11, 'move', 0, -11084.266, -1962.6666, 49.698395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 4, 12, 'move', 0, -11083.466, -1965.6, 49.898396, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 4, 13, 'move', 0, -11078.72, -1997.44, 49.96, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('lower_karazhan_halls', 4, 14, 'pull', 0, -11067.77, -2021.03, 49.88, 2.6, 0, 0, 0, 0, '61205', 'pull group 3'),
('lower_karazhan_halls', 4, 15, 'pull', 0, -11047.97, -1997.73, 49.9, 9.5, 0, 0, 0, 0, '61205,61207,61206,61200', 'pull group 4'),
('lower_karazhan_halls', 4, 16, 'move', 0, -11032.27, -1995.2, 48.6, 0, 0, 0, 0, 0, '', 'pull group 5 approach'),
('lower_karazhan_halls', 4, 17, 'pull', 0, -11016.9, -1981.84, 48.53, 1, 0, 0, 0, 0, '61208', 'pull group 5'),
('lower_karazhan_halls', 4, 18, 'pull', 0, -11019.6, -1966.94, 48.53, 4.9, 0, 0, 0, 0, '61207,61206', 'pull group 6'),
('lower_karazhan_halls', 4, 19, 'move', 0, -10990.71, -1971.73, 46.07, 0, 0, 0, 0, 0, '', 'pull group 7 approach'),
('lower_karazhan_halls', 4, 20, 'pull', 0, -10983.8, -1939.13, 46.68, 6.7, 0, 0, 0, 0, '61207', 'pull group 7'),
('lower_karazhan_halls', 4, 21, 'pull', 0, -10959.7, -1941.27, 46.13, 4.5, 0, 0, 0, 0, '61206', 'pull group 8'),
('lower_karazhan_halls', 4, 22, 'pull', 0, -10958.97, -1972.84, 46.15, 3.5, 0, 0, 0, 0, '61206,61209', 'pull group 9'),
('lower_karazhan_halls', 4, 23, 'move', 0, -10942.67, -1952.27, 49.5, 0, 0, 0, 0, 0, '', 'pull group 10 approach'),
('lower_karazhan_halls', 4, 24, 'pull', 0, -10939.64, -1970.98, 49.48, 4.9, 0, 0, 0, 0, '61209,61206', 'pull group 10'),
('lower_karazhan_halls', 4, 25, 'pull', 0, -10937, -1939.73, 49.46, 4.1, 0, 0, 0, 0, '61209,61208', 'pull group 11'),
('lower_karazhan_halls', 4, 26, 'pull', 0, -10908.05, -1943.09, 49.48, 3.9, 0, 0, 0, 0, '61206', 'pull group 12'),
('lower_karazhan_halls', 4, 27, 'pull', 0, -10911.38, -1972.85, 51.24, 5.2, 0, 0, 0, 0, '61207,61206', 'pull group 13'),
('lower_karazhan_halls', 4, 28, 'pull', 0, -10931.3, -1992.74, 49.51, 1, 0, 0, 0, 0, '61205', 'pull group 14'),
('lower_karazhan_halls', 4, 29, 'move', 0, -10911.73, -2001.33, 49.5, 0, 0, 0, 0, 0, '', 'pull group 15 approach'),
('lower_karazhan_halls', 4, 30, 'pull', 0, -10881.13, -2003.98, 49.48, 7.1, 0, 0, 0, 0, '61207', 'pull group 15'),
('lower_karazhan_halls', 4, 31, 'pull', 0, -10881.5, -2027.27, 49.48, 1, 0, 0, 0, 0, '61207', 'pull group 16'),
('lower_karazhan_halls', 4, 32, 'move', 0, -10892.18, -2035.29, 49.5, 0, 0, 0, 0, 0, '', 'pull group 17 approach'),
('lower_karazhan_halls', 4, 33, 'pull', 0, -10883.7, -2052.57, 49.48, 1, 0, 0, 0, 0, '61208', 'pull group 17'),
('lower_karazhan_halls', 4, 34, 'boss', 61221, -10899.4, -2081.48, 49.47, 0, 0, 0, 0, 0, '', 'boss'),
('lower_karazhan_halls', 5, 0, 'move', 0, -10893.6, -2074.22, 49.5, 0, 0, 0, 0, 0, '', 'to'),
('lower_karazhan_halls', 5, 1, 'pull', 0, -10905.1, -2051.57, 49.48, 1, 0, 0, 0, 0, '61208', 'pull group 0'),
('lower_karazhan_halls', 5, 2, 'move', 0, -10906.04, -2007.73, 49.5, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('lower_karazhan_halls', 5, 3, 'pull', 0, -10919.97, -2015.93, 49.47, 4.4, 0, 0, 0, 0, '61207,61206,61209', 'pull group 1'),
('lower_karazhan_halls', 5, 4, 'pull', 0, -10937.2, -2044.27, 49.47, 11.7, 0, 0, 0, 0, '61207,61209,61208,61206', 'pull group 2'),
('lower_karazhan_halls', 5, 5, 'move', 0, -10934.933, -2023.2, 49.498394, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 5, 6, 'move', 0, -10935.466, -2021.6, 49.498394, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 5, 7, 'move', 0, -10948.532, -2004, 49.498394, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 5, 8, 'move', 0, -10980.16, -1995.25, 45.96, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('lower_karazhan_halls', 5, 9, 'pull', 0, -10961.1, -1986.43, 46.13, 2, 0, 0, 0, 0, '61205,61207', 'pull group 3'),
('lower_karazhan_halls', 5, 10, 'pull', 0, -10989.88, -2010.46, 46.02, 5.4, 0, 0, 0, 0, '61205,61209,61206,61207', 'pull group 4'),
('lower_karazhan_halls', 5, 11, 'pull', 0, -11018.5, -1999.21, 48.53, 1, 0, 0, 0, 0, '61209', 'pull group 5'),
('lower_karazhan_halls', 5, 12, 'move', 0, -11050.67, -2005.33, 49.9, 0, 0, 0, 0, 0, '', 'pull group 6 approach'),
('lower_karazhan_halls', 5, 13, 'pull', 0, -11051.4, -2027.73, 53.02, 2, 0, 0, 0, 0, '61210,61205', 'pull group 6'),
('lower_karazhan_halls', 6, 0, 'move', 0, -11023.47, -1984, 66.8, 0, 0, 0, 0, 0, '', 'to'),
('lower_karazhan_halls', 6, 1, 'pull', 0, -11017.85, -1971.85, 66.83, 4, 0, 0, 0, 0, '61200', 'pull group 0'),
('lower_karazhan_halls', 7, 0, 'move', 0, -10992.89, -1994.31, 80, 0, 0, 0, 0, 0, '', 'to'),
('lower_karazhan_halls', 7, 1, 'move', 0, -10992.35, -1961.33, 79.7, 0, 0, 0, 0, 0, '', 'pull group 0 approach'),
('lower_karazhan_halls', 7, 2, 'pull', 0, -10990.7, -1974.82, 79.97, 4.1, 0, 0, 0, 0, '61200', 'pull group 0'),
('lower_karazhan_halls', 7, 3, 'pull', 0, -10967.06, -1980.9, 79.97, 7.4, 0, 0, 0, 0, '61192,61194,61193', 'pull group 1'),
('lower_karazhan_halls', 7, 4, 'move', 0, -10988.27, -1989.87, 80, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('lower_karazhan_halls', 7, 5, 'pull', 0, -10972.54, -2016.53, 79.97, 5.5, 0, 0, 0, 0, '61194,61193,61192', 'pull group 2'),
('lower_karazhan_halls', 7, 6, 'pull', 0, -10948.5, -2017.29, 79.97, 3.5, 0, 0, 0, 0, '61200', 'pull group 3'),
('lower_karazhan_halls', 7, 7, 'move', 0, -10936.33, -2025.2, 81.02, 0, 0, 0, 0, 0, '', 'pull group 4 approach'),
('lower_karazhan_halls', 7, 8, 'pull', 0, -10932.2, -2010.03, 79.97, 1, 0, 0, 0, 0, '61191', 'pull group 4'),
('lower_karazhan_halls', 8, 0, 'move', 0, -10919.2, -2036.27, 86.8, 0, 0, 0, 0, 0, '', 'to'),
('lower_karazhan_halls', 8, 1, 'pull', 0, -10929.5, -2044.93, 86.07, 1.9, 0, 0, 0, 0, '61200,61205', 'pull group 0'),
('lower_karazhan_halls', 8, 2, 'move', 0, -10923.732, -2002.9333, 92.198395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 8, 3, 'move', 0, -10938.71, -1959.47, 93.8, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('lower_karazhan_halls', 8, 4, 'pull', 0, -10941.87, -1938.46, 93.82, 4.3, 0, 0, 0, 0, '61192,61193', 'pull group 1'),
('lower_karazhan_halls', 8, 5, 'move', 0, -10944, -1920, 93.9, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('lower_karazhan_halls', 8, 6, 'pull', 0, -10937.03, -1897.3, 93.82, 4.4, 0, 0, 0, 0, '61194,61193', 'pull group 2'),
('lower_karazhan_halls', 8, 7, 'move', 0, -10905.6, -1892, 93.6, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('lower_karazhan_halls', 8, 8, 'pull', 0, -10907.85, -1874.87, 94.86, 9.2, 0, 0, 0, 0, '61199', 'pull group 3'),
('lower_karazhan_halls', 8, 9, 'move', 0, -10894.666, -1861.6, 94.29839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 8, 10, 'move', 0, -10883.466, -1858.4, 94.198395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 8, 11, 'move', 0, -10879.999, -1856, 94.0984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 8, 12, 'move', 0, -10861.332, -1838.6666, 95.698395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 8, 13, 'move', 0, -10860.27, -1836, 95.77, 0, 0, 0, 0, 0, '', 'pull group 4 approach'),
('lower_karazhan_halls', 8, 14, 'pull', 0, -10850.77, -1809.32, 94.23, 5.5, 0, 0, 0, 0, '61199,61200', 'pull group 4'),
('lower_karazhan_halls', 8, 15, 'pull', 0, -10867.6, -1872.84, 97.04, 2.1, 0, 0, 0, 0, '61205,61200', 'pull group 5'),
('lower_karazhan_halls', 8, 16, 'move', 0, -10903.33, -1892.07, 93.02, 0, 0, 0, 0, 0, '', 'pull group 6 approach'),
('lower_karazhan_halls', 8, 17, 'pull', 0, -10911, -1920.41, 92.2, 5.9, 0, 0, 0, 0, '61194,61192,61191', 'pull group 6'),
('lower_karazhan_halls', 8, 18, 'move', 0, -10902.4, -1941.33, 92.7, 0, 0, 0, 0, 0, '', 'pull group 7 approach'),
('lower_karazhan_halls', 8, 19, 'pull', 0, -10900.95, -1962.59, 92.17, 3.4, 0, 0, 0, 0, '61200', 'pull group 7'),
('lower_karazhan_halls', 8, 20, 'pull', 0, -10876.5, -1943.64, 92.28, 1, 0, 0, 0, 0, '61208', 'pull group 8'),
('lower_karazhan_halls', 8, 21, 'pull', 0, -10877.4, -1955.43, 92.16, 3.3, 0, 0, 0, 0, '61200', 'pull group 9'),
('lower_karazhan_halls', 8, 22, 'pull', 0, -10883.03, -1982.18, 92.17, 4.4, 0, 0, 0, 0, '61195,61196,61197', 'pull group 10'),
('lower_karazhan_halls', 8, 23, 'move', 0, -10841.6, -1983.56, 92.2, 0, 0, 0, 0, 0, '', 'pull group 11 approach'),
('lower_karazhan_halls', 8, 24, 'pull', 0, -10834.5, -1963.59, 93.36, 7.4, 0, 0, 0, 0, '61195,61197,61196', 'pull group 11'),
('lower_karazhan_halls', 8, 25, 'pull', 0, -10828.17, -2001.12, 92.16, 4.3, 0, 0, 0, 0, '61195,61197', 'pull group 12'),
('lower_karazhan_halls', 8, 26, 'pull', 0, -10855.53, -1992.75, 92.16, 4.2, 0, 0, 0, 0, '61197,61195,61196', 'pull group 13'),
('lower_karazhan_halls', 8, 27, 'pull', 0, -10845.28, -2029.23, 91.23, 9, 0, 0, 0, 0, '61196,61197,61195', 'pull group 14'),
('lower_karazhan_halls', 8, 28, 'move', 0, -10875.47, -1998.67, 92.2, 0, 0, 0, 0, 0, '', 'pull group 15 approach'),
('lower_karazhan_halls', 8, 29, 'pull', 0, -10890.48, -2019.67, 92.17, 6.9, 0, 0, 0, 0, '61195,61196', 'pull group 15'),
('lower_karazhan_halls', 8, 30, 'move', 0, -10892.98, -2030.13, 92.2, 0, 0, 0, 0, 0, '', 'pull group 16 approach'),
('lower_karazhan_halls', 8, 31, 'pull', 0, -10907.42, -2054.92, 92.17, 4.5, 0, 0, 0, 0, '61196,61197,61195', 'pull group 16'),
('lower_karazhan_halls', 8, 32, 'move', 0, -10882.29, -2095.09, 90.98, 0, 0, 0, 0, 0, '', 'pull group 17 approach'),
('lower_karazhan_halls', 8, 33, 'pull', 0, -10853.9, -2100.51, 92.16, 8, 0, 0, 0, 0, '61196,61197,61195', 'pull group 17'),
('lower_karazhan_halls', 8, 34, 'pull', 0, -10894.4, -2080.22, 92.16, 6.4, 0, 0, 0, 0, '61197', 'pull group 18'),
('lower_karazhan_halls', 8, 35, 'move', 0, -10930.27, -2082.71, 92.2, 0, 0, 0, 0, 0, '', 'boss approach'),
('lower_karazhan_halls', 8, 36, 'boss', 61224, -10946.2, -2104, 92.71, 0, 0, 0, 0, 0, '', 'boss'),
('lower_karazhan_halls', 9, 0, 'move', 0, -10919.732, -2071.2, 92.198395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 9, 1, 'move', 0, -10918.399, -2069.3333, 92.198395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 9, 2, 'move', 0, -10903.466, -2028.2666, 92.89839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 9, 3, 'move', 0, -10900.532, -2024.7999, 92.198395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 9, 4, 'move', 0, -10899.466, -2022.9332, 92.198395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 9, 5, 'move', 0, -10889.332, -1996.2666, 92.198395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 9, 6, 'move', 0, -10889.065, -1994.9333, 92.198395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 9, 7, 'move', 0, -10887.199, -1967.7333, 93.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 9, 8, 'move', 0, -10887.199, -1966.1333, 93.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 9, 9, 'move', 0, -10889.332, -1965.6, 92.198395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 9, 10, 'move', 0, -10899.999, -1964.5333, 92.198395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 9, 11, 'move', 0, -10936.89, -1966.04, 93.33, 0, 0, 0, 0, 0, '', 'to'),
('lower_karazhan_halls', 9, 12, 'pull', 0, -10935.9, -1990.82, 92.84, 6.9, 0, 0, 0, 0, '61194,61192,61193', 'pull group 0'),
('lower_karazhan_halls', 9, 13, 'move', 0, -10943.29, -2032.8, 93.9, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('lower_karazhan_halls', 9, 14, 'pull', 0, -10973.35, -2039.74, 93.82, 8.8, 0, 0, 0, 0, '61193,61192,61194', 'pull group 1'),
('lower_karazhan_halls', 9, 15, 'move', 0, -10983.91, -2042.22, 94, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('lower_karazhan_halls', 9, 16, 'pull', 0, -10999.55, -2012.21, 93.82, 9.4, 0, 0, 0, 0, '61192,61193,61194', 'pull group 2'),
('lower_karazhan_halls', 9, 17, 'move', 0, -11003.68, -1987.36, 94.08, 0, 0, 0, 0, 0, '', 'boss approach'),
('lower_karazhan_halls', 9, 18, 'boss', 61223, -11021.7, -1970.93, 93.83, 0, 0, 0, 0, 0, '', 'boss'),
('lower_karazhan_halls', 10, 0, 'move', 0, -11035.199, -1967.7333, 96.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 1, 'move', 0, -11036.799, -1967.2, 97.79839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 2, 'move', 0, -11074.666, -1936, 91.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 3, 'move', 0, -11080.799, -1937.0667, 91.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 4, 'move', 0, -11082.666, -1938.9333, 91.79839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 5, 'move', 0, -11085.599, -1944, 92.5984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 6, 'move', 0, -11085.599, -1945.3333, 93.89839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 7, 'move', 0, -11081.599, -1968.7999, 93.39839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 8, 'move', 0, -11081.065, -1971.4667, 93.79839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 9, 'move', 0, -11076.799, -1978.3999, 96.89839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 10, 'move', 0, -11074.133, -1981.0667, 97.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 11, 'move', 0, -11071.999, -1980.2666, 97.89839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 12, 'move', 0, -11070.133, -1978.6666, 98.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 13, 'move', 0, -11067.999, -1976.5333, 99.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 14, 'move', 0, -11037.599, -1946.9333, 113.89839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 15, 'move', 0, -11019.466, -1930.4, 113.79839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 16, 'move', 0, -11003.466, -1915.4667, 118.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 17, 'move', 0, -10998.666, -1907.7333, 117.0984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 18, 'move', 0, -10998.133, -1898.6666, 122.29839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 19, 'move', 0, -10996.799, -1898.6666, 122.9984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 20, 'move', 0, -10985.332, -1900.7999, 124.9984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 21, 'move', 0, -10982.133, -1900.5333, 125.9984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 22, 'move', 0, -10965.33, -1883.73, 125.1, 0, 0, 0, 0, 0, '', 'to'),
('lower_karazhan_halls', 10, 23, 'pull', 0, -10943.65, -1876.59, 124.53, 2.7, 0, 0, 0, 0, '61200', 'pull group 0'),
('lower_karazhan_halls', 10, 24, 'move', 0, -10944.18, -1858.84, 124.6, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('lower_karazhan_halls', 10, 25, 'pull', 0, -10955.4, -1834.39, 123.3, 1, 0, 0, 0, 0, '61205', 'pull group 1'),
('lower_karazhan_halls', 10, 26, 'move', 0, -10931.68, -1817.87, 122.34, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('lower_karazhan_halls', 10, 27, 'pull', 0, -10947.2, -1812.39, 121.71, 1, 0, 0, 0, 0, '61200', 'pull group 2'),
('lower_karazhan_halls', 10, 28, 'move', 0, -10937.599, -1849.8667, 124.5984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 29, 'move', 0, -10935.199, -1854.6666, 124.5984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 30, 'move', 0, -10921.599, -1870.4, 125.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 31, 'move', 0, -10915.999, -1872.5333, 125.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 32, 'move', 0, -10896.266, -1874.6666, 125.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 33, 'move', 0, -10893.599, -1874.6666, 125.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 34, 'move', 0, -10889.065, -1873.8667, 125.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 10, 35, 'move', 0, -10865.87, -1869.33, 124.6, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('lower_karazhan_halls', 10, 36, 'pull', 0, -10872.2, -1843.15, 123.3, 1, 0, 0, 0, 0, '61210', 'pull group 3'),
('lower_karazhan_halls', 11, 0, 'move', 0, -10908.69, -1874.93, 111.4, 0, 0, 0, 0, 0, '', 'to'),
('lower_karazhan_halls', 11, 1, 'pull', 0, -10936.5, -1874.97, 110.52, 1, 0, 0, 0, 0, '61210', 'pull group 0'),
('lower_karazhan_halls', 11, 2, 'pull', 0, -10937.4, -1846, 109.26, 1, 0, 0, 0, 0, '61200', 'pull group 1'),
('lower_karazhan_halls', 11, 3, 'move', 0, -10940.266, -1831.2, 109.29839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 4, 'move', 0, -10940.532, -1829.6, 109.29839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 5, 'move', 0, -10942.399, -1790.1333, 105.79839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 6, 'move', 0, -10942.666, -1782.1333, 102.698395, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 7, 'move', 0, -10942.399, -1779.4667, 101.5984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 8, 'move', 0, -10937.599, -1773.6, 98.39839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 9, 'move', 0, -10936.266, -1772.5333, 97.5984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 10, 'move', 0, -10927.999, -1772, 94.29839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 11, 'move', 0, -10878.409, -1777.4313, 90.93336, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 12, 'move', 0, -10871.999, -1778.1333, 90.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 13, 'move', 0, -10869.866, -1777.6, 90.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 14, 'move', 0, -10858.666, -1756, 89.9984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 15, 'move', 0, -10847.999, -1754.9333, 94.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 16, 'move', 0, -10847.199, -1755.2001, 96.29839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 11, 17, 'move', 0, -10845.67, -1757.27, 97.2, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('lower_karazhan_halls', 11, 18, 'pull', 0, -10851.2, -1779.66, 97.67, 1, 0, 0, 0, 0, '61208', 'pull group 2'),
('lower_karazhan_halls', 11, 19, 'move', 0, -10847.466, -1757.8667, 97.4984, 0, 0, 0, 0, 0, '', 'approach path'),
('lower_karazhan_halls', 11, 20, 'move', 0, -10847.199, -1755.2001, 96.29839, 0, 0, 0, 0, 0, '', 'approach path'),
('lower_karazhan_halls', 11, 21, 'pull', 0, -10850.04, -1718.93, 95.05, 0.5, 0, 0, 0, 0, '61198,61205,61194,61192', 'pull group 3'),
('lower_karazhan_halls', 11, 22, 'move', 0, -10855.64, -1753.42, 91.93, 0, 0, 0, 0, 0, '', 'pull group 4 approach'),
('lower_karazhan_halls', 11, 23, 'pull', 0, -10868.94, -1729.76, 90.47, 0.5, 0, 0, 0, 0, '61205,61200,61209,61206', 'pull group 4'),
('lower_karazhan_halls', 11, 24, 'move', 0, -10880, -1728, 90.5, 0, 0, 0, 0, 0, '', 'pull group 5 approach'),
('lower_karazhan_halls', 11, 25, 'pull', 0, -10901.7, -1719.33, 90.47, 2.8, 0, 0, 0, 0, '61198', 'pull group 5'),
('lower_karazhan_halls', 11, 26, 'pull', 0, -10919, -1730.28, 90.48, 3.7, 0, 0, 0, 0, '61199,61200', 'pull group 6'),
('lower_karazhan_halls', 12, 0, 'move', 0, -10918.399, -1734.4, 90.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 12, 1, 'move', 0, -10918.399, -1735.2001, 90.4984, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 12, 2, 'move', 0, -10919.199, -1768, 78.39839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 12, 3, 'move', 0, -10919.199, -1769.3334, 78.39839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 12, 4, 'move', 0, -10917.332, -1770.6667, 78.39839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 12, 5, 'move', 0, -10879.999, -1792, 78.39839, 0, 0, 0, 0, 0, '', 'path'),
('lower_karazhan_halls', 12, 6, 'move', 0, -10863.47, -1795.73, 78.4, 0, 0, 0, 0, 0, '', 'to'),
('lower_karazhan_halls', 12, 7, 'pull', 0, -10853.93, -1789.33, 78.39, 5.5, 0, 0, 0, 0, '61205,61200', 'pull group 0'),
('lower_karazhan_halls', 12, 8, 'pull', 0, -10879.92, -1792.6, 78.39, 5.1, 0, 0, 0, 0, '61195,61194,61210', 'pull group 1'),
('lower_karazhan_halls', 12, 9, 'pull', 0, -10908.48, -1776.71, 78.39, 7.2, 0, 0, 0, 0, '61198,61193,61192,61200', 'pull group 2'),
('lower_karazhan_halls', 12, 10, 'move', 0, -10910.73, -1726.27, 90.5, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('lower_karazhan_halls', 12, 11, 'pull', 0, -10892.8, -1737.56, 90.47, 2.1, 0, 0, 0, 0, '61205,61200', 'pull group 3'),
('lower_karazhan_halls', 12, 12, 'gossip', 61225, -10891.3, -1751.09, 90.4769, 0, 0, 1, 0, 0, '', 'talk to 61225, menu index 0 (cmd gossip option 1): Moroes'' menu has no challenge option: one of the four bosses before him is not dead (his gossip checks Araxxna, Clawlord Howlfang, Grizikil and Lord Blackwald II DONE), or the bot is not in the instance.'),
('lower_karazhan_halls', 12, 13, 'boss', 61225, -10891.3, -1751.09, 90.48, 0, 0, 0, 0, 0, '', 'boss');

-- The five bosses of the plan's boss steps (map 532), for the boss rank and the dashboard: a DELETE of this map and
-- these entries, then REPLACE.
DELETE FROM `ai_dungeon_boss` WHERE `map` IN (532) AND `entry` IN (61221, 61222, 61223, 61224, 61225);
REPLACE INTO `ai_dungeon_boss` (`map`, `entry`, `name`) VALUES
(532, 61221, 'Brood Queen Araxxna'),
(532, 61222, 'Lord Blackwald II'),
(532, 61223, 'Clawlord Howlfang'),
(532, 61224, 'Grizikil'),
(532, 61225, 'Moroes');

-- Grizikil (61224) casts Grellkin Fireball 57094 on its victim every 4-6 s (boss_grizikil.cpp UpdateAI). Interrupted
-- first so the party does not eat it while the Grellkin around him die. A DELETE of this boss's rows, then INSERT.
DELETE FROM `ai_boss_tactic` WHERE `map` = 532 AND `boss_entry` IN (61224);
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `comment`) VALUES
(532, 61224, 'interrupt', 0, 57094, 0, 0, 'Grizikil: Grellkin Fireball (boss_grizikil.cpp, every 4-6 s)');
