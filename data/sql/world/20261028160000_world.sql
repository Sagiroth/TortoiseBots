-- Blackwing Lair (map 469, the 40-man raid): the plan for the run leader (.bot run), exported from turtle-agent
-- prep/raid-bwl fbd9922 (dungeons/blackwing_lair.json, status 'draft (not mapped, not live-checked)') with
--   turtle-agent plan-export --dir dungeons --mmaps <map 469 mmaps> --allow-draft --gm-cross-direct --approach-hops \
--     --rev fbd9922 --stamp 20261028160000 --out <this file> blackwing_lair
-- NO ROW IS HAND-SET: the plan, leg and step rows below are the exporter's output; this header, the ai_dungeon_boss
-- block and the ai_boss_tactic block are the prep's own (task B1, ~/TortoiseBots-audit/RAID-PREP-bwl.md).
--
-- What the prep changed from the dungeongen draft (8 legs, 64 pull groups, 275 units):
--   * 64 pull groups become 43 (273 units). The two Tortured Drakes (13976, level 40, faction 7 'Creature', a spawn
--     flag) and Master Elemental Shaper Krixix (14401, npc_flags gossip, faction 103) are in no pull. The hostiles
--     are chained where they stand within 15 yd on one floor (|dz| <= 4) and the server walk from one to the next is
--     at most 2.2 times their distance plus 8 yd, up to 16 units; a merge stops when the pack's own radius would pass
--     18 yd (the Suppression Room is a uniform field of 158 spawns: 134 whelps of four colours, 12 hatchers and
--     12 taskmasters 8-14 yd apart; the 15 yd chain alone takes 16 units across 54 yd, radius 26.9). Radius =
--     the pack's own spread from its centre + 0.5, 1.7 to 17.9 yd. 50 entries of a pull's reach (radius + 3 yd)
--     stand outside the positions listed (turtle-agent tools/dungeongen/overlap_test.go,
--     packOverlapKnownFailing["blackwing_lair"] = 50): they are the
--     next formation's whelps, technicians and taskmasters, inside the row's own radius of the entries it lists.
--   * Legs follow the instance's boss order: razorgore_the_untamed (Grethok the Controller 12557 and his two Blackwing
--     Guardsmen 14456, whose enter-combat starts instance data 0), vaelastrasz_the_corrupt (two packs of Blackwing
--     Technicians 13996), broodlord_lashlayer (two Death Talon packs in the z 428 corridor and 22 Suppression Room
--     packs), firemaw (4 packs), ebonroc (11 packs, incl. the z 457.7 side room and the z 465 hall), flamegor (no
--     spawn of its own), chromaggus (the three Death Talon Wyrmguards 12460), nefarian (no spawn).
--   * 18 approach stands on the packs' own floors, 22 yd before the pack along the walk, and one `to` per leg, so the
--     run leader does not walk toward a pack it cannot see in hops of its own; 41 path rows cut the long walks
--     (--approach-hops).
--   * Events: Razorgore is a pull (Grethok) and a boss row; Vaelastrasz is a gossip row (13020, menu index 0) and a
--     hold on instance data 1; Nefarius is a gossip row (10162, menu index 0) and Nefarian a hold on instance data 7
--     with his phase-1 drakonids. See WHAT THE RUN LEADER CANNOT DO below: none of the three can be completed by the
--     bots alone.
--   * No patrol block: no map 469 spawn has a waypoint path (creature_movement has no point for any of the 284 guids;
--     the movement type 2 spawns stand still).
--
-- plan-check (turtle-agent plan-check --mmaps <map 469 mmaps>): ok, 309 points on the navmesh, 0 failures, 0 run
-- leader hops, 0 detour warnings, 0 warnings.
-- The ROWS: 8 legs, 120 steps (67 move, 43 pull, 6 boss, 2 gossip, 2 hold), 129 rows with the plan and leg rows.
--
-- WHAT THE RUN LEADER CANNOT DO (nothing is dropped silently). Not in these rows:
--   * Razorgore's encounter: the instance keeps every non-GM player inside Razorgore's room until instance data 0 is
--     DONE (instance_blackwing_lair.cpp Update: farther than 100 yd from razorgoreRoomCenter -7596.10 -1053.41 408.15
--     or above z 420 is teleported back), and the encounter needs a player at the Orb of Domination (177808):
--     GOHello_go_orbe_domination possesses Razorgore only when he is in combat, and the 30 eggs (177807) are destroyed
--     only by the possessed Razorgore (go_oeuf_razAI OnUse). With no orb controller the pull of Grethok starts the
--     event and it cannot be finished; legs 1 to 7 are unreachable until it is. A player or a new bot capability is
--     needed (see the prep's blockers).
--   * Vaelastrasz' and Nefarius' fight starts are gossip chains of two and three menus (each selection shows the next
--     menu); the run's gossip step sends one hello and one select, so the rows carry the first level only. The holds
--     wait on the instance data the fights set, which never comes without the later menus.
--   * The leg `skip` lists (13976, 14401): no column; the run leader pulls a hostile within 15 yd of a hop point that
--     no later step owns. Krixix stands with three Death Talon Wyrmguards in the z 465 hall: if the pull takes him he
--     is fought like them.
--   * The gossip's stand, talker and retry_s: the row is `cmd gossip <npc> <lead bot> <option>`; the lead bot talks and
--     walks to the NPC itself (fail_text is in the row's comment).
--   * The raid block (40, 4 tanks / 10 healers / 26 damage), level_band 58-60, the entrance (trigger 3726 at -7672.32
--     -1107.05 396.651, portal on map 229 at 178.729 -474.412 120.565) and entrance_tele/outside_tele
--     'BlackwingLair': the run leader starts inside the instance and leads the bound party as it is.
--   * The Suppression Room's 38 devices (179784): the bots' own strategy disarms them (disarm suppression device,
--     BlackwingLairDungeonStrategies.cpp). No object step.
--   * The eight Portcullis doors (176964, 176965, 179364, 179365, 179115, 179116, 179117, 176966) all start open
--     (spawn state and template startOpen agree) and the instance opens or closes them on each boss's DONE; the lever
--     179148 has no script in the core. No door gates the route, so no wait_object or use_object step.
--
-- ai_dungeon_boss (map 469): the six boss rows of the plan and the two event holds (Vaelastrasz 13020, Nefarian 11583,
-- summoned by Lord Victor Nefarius at 40 killed adds and landing at -7495.96 -1252.40 476.80), names from the core's
-- creature_template (all rank 3).
-- ai_boss_tactic (map 469): no row. The eight bosses cast from C++ and the bots' own Blackwing Lair strategies
-- (BlackwingLairDungeonStrategies.cpp) already handle the front cone, the drakes' facing, the Flame Buffet stacks,
-- the Chromaggus afflictions and the Nefarian class calls; the trash casters (Greater Polymorph 22274, Flamestrike
-- 22275, Rain of Fire 19717, Demon Portal 22372, Blast Wave 22424) are not bosses and their interrupts are the bot
-- AI's current-target interrupts. The DELETE below removes any row a later prep would add.
--
-- Dungeon plans for the run leader (.bot run), exported by turtle-agent (plan-export). Plan format v1; the mapping is
-- docs/guides/dungeon-run-leader.md ("From plan v1 to rows"). The plan file in turtle-agent is the single source: edit it
-- there and export again, never these rows by hand. The plan row carries the unit lists cc_priority, cc_always and kill_first
-- (columns from 20261015200000_world.sql). Never edit this file once applied; export a new one.
--
-- Idempotent: each plan's rows are deleted, then inserted. The tables come from 20261014150000_world.sql.

-- blackwing_lair: map 469, 8 legs, 120 steps
DELETE FROM `ai_dungeon_step` WHERE `plan` = 'blackwing_lair';
DELETE FROM `ai_dungeon_leg` WHERE `plan` = 'blackwing_lair';
DELETE FROM `ai_dungeon_plan` WHERE `plan` = 'blackwing_lair';
INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, `kill_first`, `source`, `comment`) VALUES
('blackwing_lair', 469, '', '', '', 'turtle-agent fbd9922 dungeons/blackwing_lair.json', 'plan v1, draft (not mapped, not live-checked)');
INSERT INTO `ai_dungeon_leg` (`plan`, `leg`, `name`, `comment`) VALUES
('blackwing_lair', 0, 'razorgore_the_untamed', ''),
('blackwing_lair', 1, 'vaelastrasz_the_corrupt', ''),
('blackwing_lair', 2, 'broodlord_lashlayer', ''),
('blackwing_lair', 3, 'firemaw', ''),
('blackwing_lair', 4, 'ebonroc', ''),
('blackwing_lair', 5, 'flamegor', ''),
('blackwing_lair', 6, 'chromaggus', ''),
('blackwing_lair', 7, 'nefarian', '');
INSERT INTO `ai_dungeon_step` (`plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, `timeout_s`, `entries`, `comment`) VALUES
('blackwing_lair', 0, 0, 'move', 0, -7634.13, -1035.27, 408.55, 0, 0, 0, 0, 0, '', 'to'),
('blackwing_lair', 0, 1, 'pull', 0, -7618.63, -1020.75, 413.38, 5.2, 0, 0, 0, 0, '14456,12557', 'pull group 0'),
('blackwing_lair', 0, 2, 'move', 0, -7633.8667, -1034.1333, 408.8493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 0, 3, 'move', 0, -7634.1333, -1035.2, 408.5493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 0, 4, 'move', 0, -7633.8667, -1039.7333, 408.5493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 0, 5, 'move', 0, -7633.6, -1040.5333, 408.5493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 0, 6, 'move', 0, -7602.419, -1079.6196, 408.5454, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 0, 7, 'move', 0, -7595.7334, -1088.0001, 408.5446, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 0, 8, 'move', 0, -7592, -1096.0001, 408.5446, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 0, 9, 'move', 0, -7591.71, -1095.93, 408.65, 0, 0, 0, 0, 0, '', 'boss approach'),
('blackwing_lair', 0, 10, 'boss', 12435, -7571.69, -1088.25, 413.46, 0, 0, 0, 0, 0, '', 'boss'),
('blackwing_lair', 1, 0, 'move', 0, -7558.4, -1073.0668, 409.0446, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 1, 1, 'move', 0, -7557.8667, -1072.2668, 409.0446, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 1, 2, 'move', 0, -7552.5337, -1041.3333, 407.3493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 1, 3, 'move', 0, -7554.4, -1028.2666, 408.5493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 1, 4, 'move', 0, -7552, -1025.3333, 408.5493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 1, 5, 'move', 0, -7550.667, -1024.2666, 408.5493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 1, 6, 'move', 0, -7536.8003, -1014.1333, 408.6493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 1, 7, 'move', 0, -7532.8003, -1013.3333, 408.6493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 1, 8, 'move', 0, -7527.467, -1014.6666, 408.6493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 1, 9, 'move', 0, -7516.94, -1020.09, 408.65, 0, 0, 0, 0, 0, '', 'to'),
('blackwing_lair', 1, 10, 'pull', 0, -7497.59, -1030.54, 408.56, 11.6, 0, 0, 0, 0, '13996', 'pull group 0'),
('blackwing_lair', 1, 11, 'move', 0, -7486.39, -1012.94, 408.66, 0, 0, 0, 0, 0, '', 'pull group 1 approach'),
('blackwing_lair', 1, 12, 'pull', 0, -7464.96, -1007.94, 408.57, 11.7, 0, 0, 0, 0, '13996', 'pull group 1'),
('blackwing_lair', 1, 13, 'gossip', 13020, -7482.26, -1017.31, 408.567, 0, 0, 1, 0, 0, '', 'talk to 13020, menu index 0 (cmd gossip option 1): Vaelastrasz'' menu has no first option: instance data 0 (Razorgore) is not DONE, and his GossipHello only opens the menu then (boss_vaelastrasz.cpp).'),
('blackwing_lair', 1, 14, 'hold', 1, -7482.26, -1017.31, 408.567, 45, 0, 3, 40, 600, '13020', 'boss hold: the event''s mobs are fought on the spot until instance data 1 reads 3 (DONE)'),
('blackwing_lair', 2, 0, 'move', 0, -7487.2, -1038.1333, 408.6493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 2, 1, 'move', 0, -7487.2, -1047.4667, 408.6493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 2, 2, 'move', 0, -7482.4004, -1064, 408.6493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 2, 3, 'move', 0, -7482.1333, -1065.3333, 408.6493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 2, 4, 'move', 0, -7482.667, -1065.6, 410.2493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 2, 5, 'move', 0, -7483.7334, -1065.0667, 411.2493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 2, 6, 'move', 0, -7512.6406, -1025.3901, 420.74332, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 2, 7, 'move', 0, -7524.5337, -1009.06665, 424.6493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 2, 8, 'move', 0, -7530.667, -1000.5333, 424.7493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 2, 9, 'move', 0, -7531.2, -999.73334, 424.8493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 2, 10, 'move', 0, -7530.667, -997.86664, 424.8493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 2, 11, 'move', 0, -7522.9336, -976.8, 424.6493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 2, 12, 'move', 0, -7528, -952.26666, 429.6493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 2, 13, 'move', 0, -7530.39, -940.07, 428.12, 0, 0, 0, 0, 0, '', 'to'),
('blackwing_lair', 2, 14, 'pull', 0, -7532.15, -918.16, 427.96, 10.1, 0, 0, 0, 0, '12464,12465,12467,12463', 'pull group 0'),
('blackwing_lair', 2, 15, 'pull', 0, -7580.07, -934.61, 428.22, 11.8, 0, 0, 0, 0, '12463,12465,12464,12467', 'pull group 1'),
('blackwing_lair', 2, 16, 'move', 0, -7631.77, -953.38, 440.17, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('blackwing_lair', 2, 17, 'pull', 0, -7645.39, -970.66, 440.22, 11.9, 0, 0, 0, 0, '14022,14024,14023,12468,14025', 'pull group 2'),
('blackwing_lair', 2, 18, 'pull', 0, -7627.13, -981.33, 440.02, 2.8, 0, 0, 0, 0, '14023,14024,14025', 'pull group 3'),
('blackwing_lair', 2, 19, 'pull', 0, -7614.48, -997.46, 440.19, 6, 0, 0, 0, 0, '14022,14023,14025,12468', 'pull group 4'),
('blackwing_lair', 2, 20, 'move', 0, -7634.16, -990.48, 440.22, 0, 0, 0, 0, 0, '', 'pull group 5 approach'),
('blackwing_lair', 2, 21, 'pull', 0, -7635.57, -1012.43, 440.2, 11, 0, 0, 0, 0, '14025,14023,14024,12468,14022,12458', 'pull group 5'),
('blackwing_lair', 2, 22, 'pull', 0, -7648.11, -996.13, 440.11, 11.2, 0, 0, 0, 0, '14022,14023,14024,14025', 'pull group 6'),
('blackwing_lair', 2, 23, 'pull', 0, -7672.38, -989.83, 440.21, 11.2, 0, 0, 0, 0, '14022,14024,14023,12468,14025', 'pull group 7'),
('blackwing_lair', 2, 24, 'move', 0, -7665.47, -1004.57, 440.09, 0, 0, 0, 0, 0, '', 'pull group 8 approach'),
('blackwing_lair', 2, 25, 'pull', 0, -7686.66, -1010.51, 440.05, 14.7, 0, 0, 0, 0, '14025,14023,14024,12458,14022,12468', 'pull group 8'),
('blackwing_lair', 2, 26, 'pull', 0, -7667.95, -1008.36, 440.26, 2.5, 0, 0, 0, 0, '14024,14023,14022', 'pull group 9'),
('blackwing_lair', 2, 27, 'pull', 0, -7661.23, -1029.12, 440.2, 11.4, 0, 0, 0, 0, '14025,14023,14024,12468,14022', 'pull group 10'),
('blackwing_lair', 2, 28, 'pull', 0, -7687.1, -1047, 440.38, 2.7, 0, 0, 0, 0, '14025,14023,14024,14022', 'pull group 11'),
('blackwing_lair', 2, 29, 'pull', 0, -7704.25, -1022.74, 440.5, 12.4, 0, 0, 0, 0, '14023,14024,14025,14022', 'pull group 12'),
('blackwing_lair', 2, 30, 'move', 0, -7699.04, -1052.51, 443.17, 0, 0, 0, 0, 0, '', 'pull group 13 approach'),
('blackwing_lair', 2, 31, 'pull', 0, -7713.51, -1068.95, 445.18, 10, 0, 0, 0, 0, '14024,14023,14025,14022', 'pull group 13'),
('blackwing_lair', 2, 32, 'pull', 0, -7702.03, -1089.35, 449.12, 1.7, 0, 0, 0, 0, '12458', 'pull group 14'),
('blackwing_lair', 2, 33, 'pull', 0, -7671.89, -1091.21, 449.15, 12.6, 0, 0, 0, 0, '14024,14025,14023,14022', 'pull group 15'),
('blackwing_lair', 2, 34, 'move', 0, -7677.46, -1098.78, 450.09, 0, 0, 0, 0, 0, '', 'pull group 16 approach'),
('blackwing_lair', 2, 35, 'pull', 0, -7657.47, -1107.93, 449.17, 11.9, 0, 0, 0, 0, '14023,14022,14024,14025', 'pull group 16'),
('blackwing_lair', 2, 36, 'pull', 0, -7685.16, -1115.16, 449.12, 14.4, 0, 0, 0, 0, '14025,14024,14023,12468,14022', 'pull group 17'),
('blackwing_lair', 2, 37, 'move', 0, -7647.64, -1095.18, 449.22, 0, 0, 0, 0, 0, '', 'pull group 18 approach'),
('blackwing_lair', 2, 38, 'pull', 0, -7625.8, -1092.54, 449.16, 10.3, 0, 0, 0, 0, '12468,14022,14025,12458', 'pull group 18'),
('blackwing_lair', 2, 39, 'pull', 0, -7638.45, -1069.75, 449.17, 13.6, 0, 0, 0, 0, '14024,14025,14023,14022,12468', 'pull group 19'),
('blackwing_lair', 2, 40, 'pull', 0, -7605.57, -1071.93, 449.17, 17.4, 0, 0, 0, 0, '14022,14023,14024,12468,14025', 'pull group 20'),
('blackwing_lair', 2, 41, 'move', 0, -7627.11, -1064.13, 449.25, 0, 0, 0, 0, 0, '', 'pull group 21 approach'),
('blackwing_lair', 2, 42, 'pull', 0, -7614.98, -1045.78, 449.17, 10.2, 0, 0, 0, 0, '14022,14023,14025,12468,14024', 'pull group 21'),
('blackwing_lair', 2, 43, 'pull', 0, -7598.2, -1030.2, 449.17, 2.2, 0, 0, 0, 0, '14023,14024,14025', 'pull group 22'),
('blackwing_lair', 2, 44, 'move', 0, -7597.16, -1063.11, 449.25, 0, 0, 0, 0, 0, '', 'pull group 23 approach'),
('blackwing_lair', 2, 45, 'pull', 0, -7575.17, -1062.37, 449.16, 6.4, 0, 0, 0, 0, '12468,14022,14024,14023', 'pull group 23'),
('blackwing_lair', 2, 46, 'boss', 12017, -7573.89, -1035.25, 449.25, 0, 0, 0, 0, 0, '', 'boss'),
('blackwing_lair', 3, 0, 'move', 0, -7569.35, -1034.24, 449.9, 0, 0, 0, 0, 0, '', 'to'),
('blackwing_lair', 3, 1, 'pull', 0, -7550.57, -1039.98, 449.24, 3.7, 0, 0, 0, 0, '13996,12459', 'pull group 0'),
('blackwing_lair', 3, 2, 'pull', 0, -7547.78, -1015.28, 449.28, 3.8, 0, 0, 0, 0, '13996,12459', 'pull group 1'),
('blackwing_lair', 3, 3, 'move', 0, -7534.04, -1043.53, 450.14, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('blackwing_lair', 3, 4, 'pull', 0, -7513.81, -1052.13, 449.26, 15.8, 0, 0, 0, 0, '12457,13996,12459', 'pull group 2'),
('blackwing_lair', 3, 5, 'move', 0, -7526.4565, -1003.75586, 449.26855, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 3, 6, 'move', 0, -7529.01, -993.99, 449.25, 0, 0, 0, 0, 0, '', 'pull group 3 approach'),
('blackwing_lair', 3, 7, 'pull', 0, -7530.43, -972.03, 449.32, 16.2, 0, 0, 0, 0, '12459,13996,12461,12457', 'pull group 3'),
('blackwing_lair', 3, 8, 'boss', 11983, -7506.4, -1015.7, 448.91, 0, 0, 0, 0, 0, '', 'boss'),
('blackwing_lair', 4, 0, 'move', 0, -7504.83, -1018.78, 449.07, 0, 0, 0, 0, 0, '', 'to'),
('blackwing_lair', 4, 1, 'pull', 0, -7494.86, -1038.39, 449.24, 2.3, 0, 0, 0, 0, '12461,12459', 'pull group 0'),
('blackwing_lair', 4, 2, 'pull', 0, -7464.9, -990.94, 449.79, 7.7, 0, 0, 0, 0, '12460,12461', 'pull group 1'),
('blackwing_lair', 4, 3, 'move', 0, -7499.308, -955.17017, 455.52362, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 4, 4, 'move', 0, -7511.4, -942.6, 456.08, 0, 0, 0, 0, 0, '', 'pull group 2 approach'),
('blackwing_lair', 4, 5, 'pull', 0, -7522.47, -923.75, 457.67, 9.2, 0, 0, 0, 0, '13996,12459,12457', 'pull group 2'),
('blackwing_lair', 4, 6, 'pull', 0, -7506.98, -906.91, 457.69, 9.9, 0, 0, 0, 0, '12461,13996,12459', 'pull group 3'),
('blackwing_lair', 4, 7, 'move', 0, -7486.61, -896.41, 462.73, 0, 0, 0, 0, 0, '', 'pull group 4 approach'),
('blackwing_lair', 4, 8, 'pull', 0, -7476.25, -877.14, 465, 17.9, 0, 0, 0, 0, '13996,12459,12461,12457', 'pull group 4'),
('blackwing_lair', 4, 9, 'pull', 0, -7453.71, -863.35, 465.07, 3.3, 0, 0, 0, 0, '12459,13996', 'pull group 5'),
('blackwing_lair', 4, 10, 'move', 0, -7435.84, -904.58, 466.17, 0, 0, 0, 0, 0, '', 'pull group 6 approach'),
('blackwing_lair', 4, 11, 'pull', 0, -7413.76, -907.85, 465.02, 15.2, 0, 0, 0, 0, '13996,12459,12457,12461', 'pull group 6'),
('blackwing_lair', 4, 12, 'pull', 0, -7398.26, -933.04, 464.98, 4, 0, 0, 0, 0, '13996,12459', 'pull group 7'),
('blackwing_lair', 4, 13, 'move', 0, -7409.3, -942.58, 465.07, 0, 0, 0, 0, 0, '', 'pull group 8 approach'),
('blackwing_lair', 4, 14, 'pull', 0, -7396.7, -960.62, 465.04, 10.2, 0, 0, 0, 0, '12460', 'pull group 8'),
('blackwing_lair', 4, 15, 'pull', 0, -7439.1, -962.88, 464.98, 4.6, 0, 0, 0, 0, '12459,13996', 'pull group 9'),
('blackwing_lair', 4, 16, 'pull', 0, -7456.27, -939, 465, 15.7, 0, 0, 0, 0, '12457,12461,13996,12459', 'pull group 10'),
('blackwing_lair', 4, 17, 'move', 0, -7421.333, -956.26666, 466.84412, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 4, 18, 'move', 0, -7404.8, -977.6, 473.0441, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 4, 19, 'move', 0, -7378.83, -986.92, 475.32, 0, 0, 0, 0, 0, '', 'boss approach'),
('blackwing_lair', 4, 20, 'boss', 14601, -7358.19, -994.32, 477.35, 0, 0, 0, 0, 0, '', 'boss'),
('blackwing_lair', 5, 0, 'move', 0, -7390.24, -1017.98, 477.2, 0, 0, 0, 0, 0, '', 'to'),
('blackwing_lair', 5, 1, 'boss', 11981, -7407.94, -1031.04, 477.35, 0, 0, 0, 0, 0, '', 'boss'),
('blackwing_lair', 6, 0, 'move', 0, -7422.76, -1036.62, 477.1, 0, 0, 0, 0, 0, '', 'to'),
('blackwing_lair', 6, 1, 'pull', 0, -7443.34, -1044.37, 477.01, 11.8, 0, 0, 0, 0, '12460', 'pull group 0'),
('blackwing_lair', 6, 2, 'move', 0, -7497.47, -1042.45, 476.65, 0, 0, 0, 0, 0, '', 'boss approach'),
('blackwing_lair', 6, 3, 'boss', 14020, -7515.34, -1029.62, 476.73, 0, 0, 0, 0, 0, '', 'boss'),
('blackwing_lair', 7, 0, 'move', 0, -7509.3335, -1045.3333, 476.6493, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 7, 1, 'move', 0, -7483.175, -1087.9445, 476.6464, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 7, 2, 'move', 0, -7466.9336, -1114.4001, 476.6446, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 7, 3, 'move', 0, -7467.7334, -1129.0668, 477.6446, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 7, 4, 'move', 0, -7488, -1152.0001, 476.54462, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 7, 5, 'move', 0, -7525.9824, -1184.5165, 476.7023, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 7, 6, 'move', 0, -7560.2666, -1213.8668, 476.8446, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 7, 7, 'move', 0, -7573.3335, -1237.3335, 476.8446, 0, 0, 0, 0, 0, '', 'path'),
('blackwing_lair', 7, 8, 'move', 0, -7578.04, -1246.02, 478.13, 0, 0, 0, 0, 0, '', 'to'),
('blackwing_lair', 7, 9, 'gossip', 10162, -7588.27, -1261.92, 482.03, 0, 0, 1, 0, 0, '', 'talk to 10162, menu index 0 (cmd gossip option 1): Lord Victor Nefarius'' menu has no mistake option: instance data 6 (Chromaggus) is not DONE (boss_victor_nefarius.cpp GossipHello).'),
('blackwing_lair', 7, 10, 'hold', 7, -7545, -1250, 476.9, 90, 0, 3, 40, 600, '11583,14261,14262,14263,14264,14265,14302', 'boss hold: the event''s mobs are fought on the spot until instance data 7 reads 3 (DONE)');

-- The plan's eight bosses (map 469), for the boss rank and the dashboard: a DELETE scoped to this map and these
-- entries, then REPLACE.
DELETE FROM `ai_dungeon_boss` WHERE `map` IN (469) AND `entry` IN (12435, 13020, 12017, 11983, 14601, 11981, 14020, 11583);
REPLACE INTO `ai_dungeon_boss` (`map`, `entry`, `name`) VALUES
(469, 12435, 'Razorgore the Untamed'),
(469, 13020, 'Vaelastrasz the Corrupt'),
(469, 12017, 'Broodlord Lashlayer'),
(469, 11983, 'Firemaw'),
(469, 14601, 'Ebonroc'),
(469, 11981, 'Flamegor'),
(469, 14020, 'Chromaggus'),
(469, 11583, 'Nefarian');

-- No tactic row for this map (see the header): the DELETE removes any row a later prep would add.
DELETE FROM `ai_boss_tactic` WHERE `map` = 469 AND `boss_entry` IN (12435, 13020, 12017, 11983, 14601, 11981, 14020, 11583);
