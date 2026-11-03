-- Emerald Sanctum (map 807, the 40-man raid): the plan for the run leader (.bot run), exported from turtle-agent
-- prep/raid-es ae49e6a (dungeons/emerald_sanctum.json, status 'draft (not mapped, not live-checked)') with
--   turtle-agent plan-export --dir dungeons --mmaps <map 807 mmaps> --allow-draft --gm-cross-direct --approach-hops \
--     --rev ae49e6a --stamp 20261028170000 --out <this file> emerald_sanctum
-- NO ROW IS HAND-SET: the plan, leg and step rows below are the exporter's output; this header and the
-- ai_dungeon_boss block are the prep's own (task B1, ~/TortoiseBots-audit/RAID-PREP-es.md).
--
-- What the prep changed from the dungeongen draft (2 legs, 69 pull groups):
--   * The world DB's 158 spawns of map 807 are 10 entries: 85 hostile trash units in 10 creature_linking groups
--     (flag 3, 5-12 units each) plus 3 lone waypoint patrols (60744 Sanctum Wyrm guid 2570738, 60746 Sanctum
--     Scalebane guids 2570819 and 2570821), and 71 critters (883 Deer 44, 1420 Toad 27: level 5 and 1, factions 28
--     'Prey' and 148 'Ambient'). The draft's 69 pull groups (critters and the linked packs' units split apart)
--     become 13: one per linked group and one per patrol. A patrol is never merged into a trash pull. Radius = the
--     pack's own spread from its centre + 0.5, at most 35 (the largest here is 17.2); no pull holds more than 12
--     units (the 40-man cap is 16). The critters are in no pull and in each leg's skip list.
--   * Boss order: Erennius (60747) before Solnius (60748). Solnius' EnterCombat sets in-combat-with-zone on
--     Erennius and turns hard mode on while Erennius lives, and puts every living trash unit of the map in combat
--     with the attacker (instance_emerald_sanctum::GetTrashGUID), so every pull comes before Solnius and Erennius
--     dies before him.
--   * Solnius is friendly (faction 35) with npc_flags 1 until the one item of his menu (92950) is taken
--     (boss_solnius.cpp GossipHello/GossipSelect): the leg's gossip row talks to him (menu index 0, param 1 =
--     `cmd gossip 60748 <lead bot> 1`) and then fights.
--   * Erennius patrols a 32-point loop of 1323.5 yd (529 s at 2.5 yd/s, creature_movement guid 2570746) that
--     crosses the east hall's packs (one point 1 yd from a unit of the 12-unit pack at 3227.7 3041.3): he is
--     engaged at his spawn 3293.46 3130.04, before the east packs. A boss has no patrol block, so the loop is not in
--     the rows: the mapping walk measures where the patrol meets the raid.
--   * 9 approach stands and 5 `to` rows: every pull and boss whose walk from the last move row is over 50 yd stands
--     at a point 25-28 yd from it (the one exception, the wyrm's one-unit pull, stands 31 yd from the pack before
--     it), so the run leader does not walk toward a pack in hops of its own.
--   * 3 patrol blocks (the lone patrols) are in the plan and NOT in the rows: the run leader pulls the pack where
--     it stands.
--   * No door, lever, key or gong: the map's 3 game objects are entrance 'Ghost Gate' visuals (180322 twice,
--     2010866); no object step. Solnius' chest (2020042) and portals (177243) are script objects.
--
-- plan-check (turtle-agent plan-check --mmaps <map 807 mmaps>): ok, 101 points on the navmesh, 0 failures,
-- 3 warnings, all "the patrol of ... has only an estimated period_s": the mapping walk (B2) measures the rounds.
-- The ROWS: 5 legs, 45 steps (29 move, 13 pull, 2 boss, 1 gossip), 51 rows with the plan and leg rows.
--
-- WHAT THE RUN LEADER CANNOT DO (nothing is dropped silently). Not in these rows:
--   * the skip lists (883, 1420 on every leg: the critters): no column; a critter is not hostile, so no hop pull
--     takes it.
--   * the 3 patrol blocks and Erennius' loop: no patrol column; the run leader pulls the entry where it stands when
--     its step runs. A patrol unit that has walked away from its spawn then is not pulled, and Solnius' EnterCombat
--     engages every living trash unit: B2 watches the three patrol units and Erennius.
--   * the gossip's stand, talker and retry: the row is the order `cmd gossip 60748 <lead bot> 1`; the lead bot walks
--     to Solnius and asks once.
--   * Solnius' in-fight phases: he sleeps at 60 % and summons 3 portals with a major add and a Dreamer each, and
--     below 40 % casts Emerald Rot on 4 targets every 30 s (boss_solnius.cpp). One boss row; the bots' own
--     `solnius` strategy (EmeraldSanctumDungeonStrategies.cpp, AiPlayerbot.EnableCustomRaidTactics) flanks the Acid
--     Breath, runs Emerald Rot carriers out and focuses the adds.
--   * Erennius' hard mode: the plan kills him first, so Solnius is fought normal; no step expresses hard mode.
--   * the entrance (trigger 5017, portal at 4833.13 -1756.65 1158.04 on map 1), level_band 60-60 and the raid
--     composition 4/10/26 (placeholder): the run leader starts inside the instance and leads the bound party as it
--     is.
--
-- ai_dungeon_boss (map 807): the two boss steps' entries, names from the core's creature_template (both rank 3), so
-- the commander telemetry and the dashboard call them bosses. The DELETE is scoped to this map and the two entries;
-- REPLACE then makes a re-apply harmless.
-- ai_boss_tactic (map 807): none. Solnius' Acid Breath flank and Emerald Rot runout are the bots' own strategy (a
-- `behind` row would duplicate it); Erennius' casts and the trash casts are script-side and the Turtle Spell.dbc is
-- not in the prep's data set, so no interrupt row is invented. There was no row of this map to replace, so no
-- DELETE.

-- Dungeon plans for the run leader (.bot run), exported by turtle-agent (plan-export). Plan format v1; the mapping is
-- docs/guides/dungeon-run-leader.md ("From plan v1 to rows"). The plan file in turtle-agent is the single source: edit it
-- there and export again, never these rows by hand. The plan row carries the unit lists cc_priority, cc_always and kill_first
-- (columns from 20261015200000_world.sql). Never edit this file once applied; export a new one.
--
-- Idempotent: each plan's rows are deleted, then inserted. The tables come from 20261014150000_world.sql.

-- emerald_sanctum: map 807, 5 legs, 45 steps
DELETE FROM `ai_dungeon_step` WHERE `plan` = 'emerald_sanctum';
DELETE FROM `ai_dungeon_leg` WHERE `plan` = 'emerald_sanctum';
DELETE FROM `ai_dungeon_plan` WHERE `plan` = 'emerald_sanctum';
INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, `kill_first`, `source`, `comment`) VALUES
('emerald_sanctum', 807, '', '', '', 'turtle-agent ae49e6a dungeons/emerald_sanctum.json', 'plan v1, draft (not mapped, not live-checked)');
INSERT INTO `ai_dungeon_leg` (`plan`, `leg`, `name`, `comment`) VALUES
('emerald_sanctum', 0, 'entrance_west', ''),
('emerald_sanctum', 1, 'north_hall', ''),
('emerald_sanctum', 2, 'erennius', ''),
('emerald_sanctum', 3, 'east_sanctum', ''),
('emerald_sanctum', 4, 'solnius', '');
INSERT INTO `ai_dungeon_step` (`plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, `timeout_s`, `entries`, `comment`) VALUES
('emerald_sanctum', 0, 0, 'move', 0, 2810.0942, 3044.289, 26.093428, 0, 0, 0, 0, 0, '', 'to'),
('emerald_sanctum', 0, 1, 'pull', 0, 2830.18, 3063.76, 26.14, 8.3, 0, 0, 0, 0, '60742', 'pull group 0'),
('emerald_sanctum', 0, 2, 'pull', 0, 2847.07, 3039.03, 21.62, 1, 0, 0, 0, 0, '60744', 'pull group 1'),
('emerald_sanctum', 0, 3, 'move', 0, 2848.2698, 3016.865, 25.399967, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('emerald_sanctum', 0, 4, 'pull', 0, 2869.68, 3004.08, 24.11, 6.4, 0, 0, 0, 0, '60743,60742', 'pull group 2'),
('emerald_sanctum', 0, 5, 'move', 0, 2895.6648, 3021.7976, 24.987663, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('emerald_sanctum', 0, 6, 'pull', 0, 2923.46, 3024.69, 25.05, 9.2, 0, 0, 0, 0, '60742,60745,60743', 'pull group 3'),
('emerald_sanctum', 1, 0, 'move', 0, 2958.1333, 3047.7334, 23.120451, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 1, 1, 'move', 0, 2965.3333, 3050.6667, 21.920452, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 1, 2, 'move', 0, 2997.4736, 3064.455, 24.378767, 0, 0, 0, 0, 0, '', 'to'),
('emerald_sanctum', 1, 3, 'pull', 0, 3019.76, 3080.79, 26.75, 17.2, 0, 0, 0, 0, '60742,60745', 'pull group 0'),
('emerald_sanctum', 1, 4, 'move', 0, 3033.4746, 3037.5015, 24.61062, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('emerald_sanctum', 1, 5, 'pull', 0, 3055.98, 3021.02, 24.4, 1, 0, 0, 0, 0, '60746', 'pull group 1'),
('emerald_sanctum', 1, 6, 'move', 0, 3057.3333, 3040, 24.720451, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 1, 7, 'move', 0, 3058.1333, 3060, 23.020452, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 1, 8, 'move', 0, 3056.5105, 3083.849, 26.58175, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('emerald_sanctum', 1, 9, 'pull', 0, 3066.9, 3109.73, 25.6, 1, 0, 0, 0, 0, '60746', 'pull group 2'),
('emerald_sanctum', 1, 10, 'move', 0, 3059.392, 3137.0825, 25.492125, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('emerald_sanctum', 1, 11, 'pull', 0, 3059, 3164.84, 25.49, 6.3, 0, 0, 0, 0, '60746,60745,60743,61212', 'pull group 3'),
('emerald_sanctum', 1, 12, 'move', 0, 3090.4011, 3125.934, 26.184408, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 1, 13, 'move', 0, 3103.442, 3109.7769, 26.409813, 0, 0, 0, 0, 0, '', 'pull group 4 approach'),
('emerald_sanctum', 1, 14, 'pull', 0, 3128.83, 3098.05, 25.28, 15.8, 0, 0, 0, 0, '60742,60744,60745', 'pull group 4'),
('emerald_sanctum', 2, 0, 'move', 0, 3176.5332, 3111.7334, 26.120451, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 2, 1, 'move', 0, 3225.877, 3119.8003, 25.773134, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 2, 2, 'move', 0, 3265.7485, 3126.3186, 25.492489, 0, 0, 0, 0, 0, '', 'to'),
('emerald_sanctum', 2, 3, 'boss', 60747, 3293.46, 3130.04, 24.5068, 0, 0, 0, 0, 0, '', 'boss'),
('emerald_sanctum', 3, 0, 'move', 0, 3266.5173, 3149.079, 25.962868, 0, 0, 0, 0, 0, '', 'to'),
('emerald_sanctum', 3, 1, 'pull', 0, 3266.55, 3176.98, 24.37, 14.3, 0, 0, 0, 0, '60742,60745,60746', 'pull group 0'),
('emerald_sanctum', 3, 2, 'move', 0, 3249.8582, 3129.8535, 23.976822, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 3, 3, 'move', 0, 3233.1663, 3082.727, 23.281803, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 3, 4, 'move', 0, 3228.8, 3070.4, 23.1, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 3, 5, 'move', 0, 3228.5469, 3069.239, 22.715591, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('emerald_sanctum', 3, 6, 'pull', 0, 3227.7, 3041.33, 21.59, 14.1, 0, 0, 0, 0, '60742,60743,61212', 'pull group 1'),
('emerald_sanctum', 3, 7, 'move', 0, 3172.82, 3037.6116, 21.777557, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('emerald_sanctum', 3, 8, 'pull', 0, 3147.57, 3026.6, 25.12, 9.3, 0, 0, 0, 0, '60742', 'pull group 2'),
('emerald_sanctum', 3, 9, 'move', 0, 3165.9707, 2986.3857, 24.594904, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('emerald_sanctum', 3, 10, 'pull', 0, 3152.55, 2962.87, 25.09, 10.8, 0, 0, 0, 0, '60742,61212,60746,60743', 'pull group 3'),
('emerald_sanctum', 4, 0, 'move', 0, 3157.3333, 2965.3333, 25.220451, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 4, 1, 'move', 0, 3200, 2986.6667, 24.7, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 4, 2, 'move', 0, 3225.8667, 3001.0667, 24.7, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 4, 3, 'move', 0, 3271.0059, 3022.523, 26.142231, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 4, 4, 'move', 0, 3285.3333, 3029.3333, 26.6, 0, 0, 0, 0, 0, '', 'path'),
('emerald_sanctum', 4, 5, 'move', 0, 3293.9514, 3032.6064, 25.770536, 0, 0, 0, 0, 0, '', 'to'),
('emerald_sanctum', 4, 6, 'gossip', 60748, 3320.08, 3042.3, 25.274, 0, 0, 1, 0, 0, '', 'talk to 60748, menu index 0 (cmd gossip option 1): Solnius'' menu has no start option (boss_solnius.cpp GossipHello sends menu 92950 with one item): the bot is not in the instance or the gossip script did not load.'),
('emerald_sanctum', 4, 7, 'boss', 60748, 3320.08, 3042.3, 25.274, 0, 0, 0, 0, 0, '', 'boss');

-- The two bosses of the plan's boss steps (map 807), for the boss rank and the dashboard: a DELETE of this map and
-- these entries, then REPLACE.
DELETE FROM `ai_dungeon_boss` WHERE `map` IN (807) AND `entry` IN (60747, 60748);
REPLACE INTO `ai_dungeon_boss` (`map`, `entry`, `name`) VALUES
(807, 60747, 'Erennius'),
(807, 60748, 'Solnius');
