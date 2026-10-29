-- Boss stand sets (Onyxia kill plan, code item C3): where bots of a role stand while a boss is at an
-- anchor and casting the row's window spell. Read once at startup (ai/playerbot/BossStandMgr.cpp,
-- LoadBossStands); `.bot run reloadplans` does not reload it, a restart does.
--
--   anchor        id of the place the boss stops at within its boss_entry; the rows of one id share
--                 anchor_x / anchor_y / anchor_r (the boss is "at" the anchor within anchor_r yd, 2D)
--   role          ranged | healer | melee (PlayerbotAI: tank = melee, healer before ranged)
--   window_spell  0 = always; else the row only applies while the boss is casting this spell
--   aura          0 = none; else the boss has to wear this aura
--   side, seq     the side of the boss's lane the point lies on (0 or 1) and the point's order there
--   x, y, z       the point (z = the floor there; the module snaps it to the ground again before a move)
--   dx, dy        direction the seats of the point line up in, 9 yd apart (BossStandPolicy.h
--                 kBossStandSpacing); 0 0 = straight out from the anchor
--   slots         bots the point seats (1-8)
--   window_signal how the window opens: 'cast' (the boss is casting window_spell, read from its current spell) or
--                 'stopped_no_target' (the boss wears the aura, stands still and has no target: the state a
--                 TRIGGERED cast leaves; a triggered spell never shows as the boss's current spell)
--   hold_ms       a window row's seat outlives the cast by up to this long (default 8000) ...
--   release_anchor ... or until the boss is within the radius of this anchor (the lane's far end; 0 = none),
--                 whichever comes first. The breath's links keep firing along the lane for seconds after the
--                 cast ends, so a bot that walked back the moment the cast ended would walk under the boss.
--                 During the hold the caster reach does not move the bot (it may cast from its seat); a new
--                 window (the next breath, at any anchor) re-seats from that anchor.
-- A bot takes the side it stands on (it never crosses the lane for its point) and the nearest point of
-- it with a free seat; a full side sends it to the other side; a full table seats it nowhere and it
-- keeps the behaviour it had. No row applying changes nothing for any bot.
--
-- Onyxia (10184, map 249). Her phase 2 stops her at one of 8 hover points (core boss_onyxia.cpp
-- aMoveData) and, 30 % of the times she moves, casts the Deep Breath of that point (5 s cast, the
-- spell of the point she is AT, set apart below) before flying the lane to the opposite point; each link
-- hits everything within 30 yd of its point (radius index 10) for 1275-1725 fire, so the whole lane
-- 60+ yd wide kills. The only safe place is 30 yd or more from every link. The module's old far
-- spots (OnyxiasLairDungeonActions.cpp kBreathSafeSpots) sit 50-76 yd from the hover point, out of any cast
-- range, so a standing stand there would have ended the ranged damage C1 restored; these rows are
-- WINDOWED instead: they apply only while she breathes (window_spell = the point's own breath id,
-- aura = Hover 17131; the core casts the breath TRIGGERED, boss_onyxia.cpp:522, so it never shows as her
-- current spell: the window opens on the state the cast leaves, window_signal 'stopped_no_target': she wears
-- Hover, stands still and has no target, the same signal the module's reactive dodge uses), and the row holds a few seconds past the cast
-- (hold_ms 8000, or until she is at the opposite hover point, release_anchor), after which the bot goes back
-- to C1's reach without a walk back.
-- The cast is 5 s (and the hold a few more), so the points are the NEAREST walkable floor that is 31 yd or
-- more from the lane (a segment from the hover point to the opposite one; the N to S lane runs on into
-- the tunnel to -130.8 -213.4), never the old far spots. Only ranged and healers have rows (melee stays
-- on her when she lands). Both roles have seats on BOTH sides of the lane (a bot takes the side of the lane
-- it stands on and never crosses it for a seat): ranged take the points nearest the usual firing ring of
-- the anchor (about 20 yd from her footprint toward the raid stand -80.0 -211.4), healers the next ones of
-- the same side, a side with a single point is split between them. The room is
-- only 85 yd deep (y -250..-165) so a lane through its middle leaves bands 4-19 yd deep and the N
-- and S anchors (lane along y -215) seat fewer bots; what does not fit keeps the reactive dodge.
--
-- Placement rules checked here and by tools/test_boss_stand_sql.py: every seat 31 yd or more from the lane
-- segment, none in an egg field (x -119..-29 with y < -237 or y > -183, a stepped-on egg hatches whelps),
-- every seat of an anchor 9 yd or more from every other (Fireball 18392 splashes 8 yd).
--
-- Path-check (turtle-agent path-check --mmaps ~/turtle-agent-mmaps --map 249, the server's own path
-- query) from the raid stand -80.0 -211.4 -83.0 to EVERY seat of EVERY row: type ok, complete, the end
-- on the mesh within 1 yd. The row comment gives the stand-to-first-seat walk. From the usual firing
-- ring (point at 19.6 yd from the footprint toward the stand, and +-70 degrees round it) the walk to
-- the nearest point of each side is, F0 / F+70 / F-70 in yd ("-" = that ring point is off the mesh;
-- UB = the straight line passes under the hover point, harmless 20 yd below her):
--   NE side 0: 44  /  -  /  63
--   NE side 1: 38  /  -  /  17
--   E  side 0: 17  /  -  /  -
--   E  side 1: 47  /  -  /  -
--   SE side 0: 13  /  -  /  33
--   SE side 1: 54  /  -  /  36
--   S  side 0: -  /  64  /  39
--   S  side 1: -  /  51  /  68
--   SW side 0: 53  /  39  /  -
--   SW side 1: 12  /  31  /  -
--   W  side 0: 48  /  -  /  61
--   W  side 1: 16  /  -  /  27
--   NW side 0: -  /  19  /  -
--   NW side 1: -  /  55  /  -
--   N  side 0: 32  /  14  /  50
--   N  side 1: 32  /  49  /  12
-- Walks of 12-17 yd (a ring point on the point's own side of the lane) fit the 5 s cast; the rest are
-- 30-68 yd and are a bot that stands in or across the lane when the cast starts: it still starts at once
-- and runs at 7 yd/s, which is why the old far spots (50-76 yd) could not work and these nearest points are used.
--
-- Anchor ids are the core's hover point id + 1 (anchor 1 = point 0 NE ... 8 = point 7 N); release_anchor of an
-- anchor is the id of the opposite hover point.
--
-- Idempotent: the DELETE removes only these rows' boss. The core runs each migration file inside one
-- transaction. Never edit this file once applied; add a newer migration.

CREATE TABLE IF NOT EXISTS `ai_boss_stand` (
  `map` int unsigned NOT NULL,
  `boss_entry` int unsigned NOT NULL,
  `anchor` int unsigned NOT NULL,
  `anchor_x` float NOT NULL,
  `anchor_y` float NOT NULL,
  `anchor_r` float NOT NULL,
  `role` varchar(8) NOT NULL,
  `window_spell` int unsigned NOT NULL DEFAULT 0,
  `aura` int unsigned NOT NULL DEFAULT 0,
  `side` tinyint unsigned NOT NULL DEFAULT 0,
  `seq` tinyint unsigned NOT NULL DEFAULT 0,
  `x` float NOT NULL,
  `y` float NOT NULL,
  `z` float NOT NULL,
  `dx` float NOT NULL DEFAULT 0,
  `dy` float NOT NULL DEFAULT 0,
  `slots` tinyint unsigned NOT NULL DEFAULT 5,
  `hold_ms` int unsigned NOT NULL DEFAULT 8000,
  `release_anchor` int unsigned NOT NULL DEFAULT 0,
  `window_signal` varchar(24) NOT NULL DEFAULT 'cast',
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`map`, `boss_entry`, `anchor`, `role`, `window_spell`, `side`, `seq`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

DELETE FROM `ai_boss_stand` WHERE `map` = 249 AND `boss_entry` = 10184;

INSERT INTO `ai_boss_stand` (`map`, `boss_entry`, `anchor`, `anchor_x`, `anchor_y`, `anchor_r`, `role`, `window_spell`, `aura`, `side`, `seq`, `x`, `y`, `z`, `dx`, `dy`, `slots`, `hold_ms`, `release_anchor`, `window_signal`, `comment`) VALUES
-- Hover point 0 NE (anchor 1) (10.2 -247.9), breath 18617
(249, 10184, 1, 10.2191, -247.9120, 8, 'ranged', 18617, 17131, 0, 0, -52.0, -236.0, -85.0, -0.760, 0.650, 5, 8000, 5, 'stopped_no_target', 'Onyxia NE ranged side 0 point 0: 5 slots, stand to point 37 yd, path-check ok'),
(249, 10184, 1, 10.2191, -247.9120, 8, 'ranged', 18617, 17131, 1, 0, 14.0, -210.0, -85.1, -0.760, 0.650, 5, 8000, 5, 'stopped_no_target', 'Onyxia NE ranged side 1 point 0: 5 slots, stand to point 94 yd, path-check ok'),
(249, 10184, 1, 10.2191, -247.9120, 8, 'ranged', 18617, 17131, 1, 1, 24.0, -206.0, -84.2, -0.760, 0.650, 5, 8000, 5, 'stopped_no_target', 'Onyxia NE ranged side 1 point 1: 5 slots, stand to point 104 yd, path-check ok'),
(249, 10184, 1, 10.2191, -247.9120, 8, 'healer', 18617, 17131, 0, 0, -68.0, -236.0, -83.7, -0.760, 0.650, 3, 8000, 5, 'stopped_no_target', 'Onyxia NE healer side 0 point 0: 3 slots, stand to point 27 yd, path-check ok'),
(249, 10184, 1, 10.2191, -247.9120, 8, 'healer', 18617, 17131, 1, 0, 26.0, -196.0, -83.7, -0.760, 0.650, 5, 8000, 5, 'stopped_no_target', 'Onyxia NE healer side 1 point 0: 5 slots, stand to point 107 yd, path-check ok'),
(249, 10184, 1, 10.2191, -247.9120, 8, 'healer', 18617, 17131, 1, 1, 22.0, -216.0, -84.5, 0.760, -0.650, 2, 8000, 5, 'stopped_no_target', 'Onyxia NE healer side 1 point 1: 2 slots, stand to point 102 yd, path-check ok'),
(249, 10184, 1, 10.2191, -247.9120, 8, 'healer', 18617, 17131, 1, 2, -18.0, -178.0, -87.2, -0.760, 0.650, 2, 8000, 5, 'stopped_no_target', 'Onyxia NE healer side 1 point 2: 2 slots, stand to point 71 yd, path-check ok'),
(249, 10184, 1, 10.2191, -247.9120, 8, 'healer', 18617, 17131, 1, 3, 22.0, -180.0, -84.3, -0.760, 0.650, 2, 8000, 5, 'stopped_no_target', 'Onyxia NE healer side 1 point 3: 2 slots, stand to point 107 yd, path-check ok'),
-- Hover point 1 E (anchor 2) (-31.5 -250.1), breath 18576
(249, 10184, 2, -31.4963, -250.1230, 8, 'ranged', 18576, 17131, 0, 0, -64.0, -236.0, -84.1, -0.030, 1.000, 5, 8000, 6, 'stopped_no_target', 'Onyxia E ranged side 0 point 0: 5 slots, stand to point 30 yd, path-check ok'),
(249, 10184, 2, -31.4963, -250.1230, 8, 'ranged', 18576, 17131, 1, 0, 0.0, -238.0, -86.3, -0.030, 1.000, 5, 8000, 6, 'stopped_no_target', 'Onyxia E ranged side 1 point 0: 5 slots, stand to point 85 yd, path-check ok'),
(249, 10184, 2, -31.4963, -250.1230, 8, 'ranged', 18576, 17131, 1, 1, 10.0, -238.0, -85.4, -0.030, 1.000, 5, 8000, 6, 'stopped_no_target', 'Onyxia E ranged side 1 point 1: 5 slots, stand to point 94 yd, path-check ok'),
(249, 10184, 2, -31.4963, -250.1230, 8, 'healer', 18576, 17131, 0, 0, -74.0, -236.0, -82.8, -0.030, 1.000, 5, 8000, 6, 'stopped_no_target', 'Onyxia E healer side 0 point 0: 5 slots, stand to point 25 yd, path-check ok'),
(249, 10184, 2, -31.4963, -250.1230, 8, 'healer', 18576, 17131, 1, 0, 20.0, -238.0, -84.4, -0.030, 1.000, 5, 8000, 6, 'stopped_no_target', 'Onyxia E healer side 1 point 0: 5 slots, stand to point 104 yd, path-check ok'),
(249, 10184, 2, -31.4963, -250.1230, 8, 'healer', 18576, 17131, 1, 1, 30.0, -238.0, -82.5, -0.030, 1.000, 5, 8000, 6, 'stopped_no_target', 'Onyxia E healer side 1 point 1: 5 slots, stand to point 113 yd, path-check ok'),
(249, 10184, 2, -31.4963, -250.1230, 8, 'healer', 18576, 17131, 1, 2, 0.0, -248.0, -85.7, 0.030, -1.000, 2, 8000, 6, 'stopped_no_target', 'Onyxia E healer side 1 point 2: 2 slots, stand to point 88 yd, path-check ok'),
(249, 10184, 2, -31.4963, -250.1230, 8, 'healer', 18576, 17131, 1, 3, 10.0, -248.0, -85.2, 0.030, -1.000, 2, 8000, 6, 'stopped_no_target', 'Onyxia E healer side 1 point 3: 2 slots, stand to point 97 yd, path-check ok'),
-- Hover point 2 SE (anchor 3) (-63.5 -240.1), breath 18564
(249, 10184, 3, -63.5156, -240.0960, 8, 'ranged', 18564, 17131, 0, 0, -82.0, -214.0, -82.2, 0.760, 0.650, 5, 8000, 7, 'stopped_no_target', 'Onyxia SE ranged side 0 point 0: 5 slots, stand to point 3 yd, path-check ok'),
(249, 10184, 3, -63.5156, -240.0960, 8, 'ranged', 18564, 17131, 1, 0, -28.0, -252.0, -88.6, 0.760, 0.650, 5, 8000, 7, 'stopped_no_target', 'Onyxia SE ranged side 1 point 0: 5 slots, stand to point 66 yd, path-check ok'),
(249, 10184, 3, -63.5156, -240.0960, 8, 'ranged', 18564, 17131, 1, 1, -14.0, -254.0, -86.2, 0.760, 0.650, 5, 8000, 7, 'stopped_no_target', 'Onyxia SE ranged side 1 point 1: 5 slots, stand to point 79 yd, path-check ok'),
(249, 10184, 3, -63.5156, -240.0960, 8, 'healer', 18564, 17131, 0, 0, -80.0, -200.0, -82.1, 0.760, 0.650, 3, 8000, 7, 'stopped_no_target', 'Onyxia SE healer side 0 point 0: 3 slots, stand to point 11 yd, path-check ok'),
(249, 10184, 3, -63.5156, -240.0960, 8, 'healer', 18564, 17131, 1, 0, 2.0, -252.0, -84.7, 0.760, 0.650, 5, 8000, 7, 'stopped_no_target', 'Onyxia SE healer side 1 point 0: 5 slots, stand to point 92 yd, path-check ok'),
(249, 10184, 3, -63.5156, -240.0960, 8, 'healer', 18564, 17131, 1, 1, 8.0, -222.0, -85.5, 0.760, 0.650, 4, 8000, 7, 'stopped_no_target', 'Onyxia SE healer side 1 point 1: 4 slots, stand to point 89 yd, path-check ok'),
(249, 10184, 3, -63.5156, -240.0960, 8, 'healer', 18564, 17131, 1, 2, 16.0, -254.0, -84.4, 0.760, 0.650, 2, 8000, 7, 'stopped_no_target', 'Onyxia SE healer side 1 point 2: 2 slots, stand to point 105 yd, path-check ok'),
(249, 10184, 3, -63.5156, -240.0960, 8, 'healer', 18564, 17131, 1, 3, 24.0, -220.0, -84.2, 0.760, 0.650, 2, 8000, 7, 'stopped_no_target', 'Onyxia SE healer side 1 point 3: 2 slots, stand to point 104 yd, path-check ok'),
-- Hover point 3 S (anchor 4) (-65.8 -213.8), breath 18351
(249, 10184, 4, -65.8444, -213.8090, 8, 'ranged', 18351, 17131, 0, 0, -32.0, -184.0, -88.6, 1.000, -0.040, 5, 8000, 8, 'stopped_no_target', 'Onyxia S ranged side 0 point 0: 5 slots, stand to point 56 yd, path-check ok'),
(249, 10184, 4, -65.8444, -213.8090, 8, 'ranged', 18351, 17131, 0, 1, -28.0, -174.0, -88.3, 1.000, -0.040, 5, 8000, 8, 'stopped_no_target', 'Onyxia S ranged side 0 point 1: 5 slots, stand to point 64 yd, path-check ok'),
(249, 10184, 4, -65.8444, -213.8090, 8, 'ranged', 18351, 17131, 1, 0, -28.0, -248.0, -88.6, 1.000, -0.040, 3, 8000, 8, 'stopped_no_target', 'Onyxia S ranged side 1 point 0: 3 slots, stand to point 64 yd, path-check ok'),
(249, 10184, 4, -65.8444, -213.8090, 8, 'healer', 18351, 17131, 0, 0, 14.0, -184.0, -85.0, 1.000, -0.040, 2, 8000, 8, 'stopped_no_target', 'Onyxia S healer side 0 point 0: 2 slots, stand to point 98 yd, path-check ok'),
(249, 10184, 4, -65.8444, -213.8090, 8, 'healer', 18351, 17131, 1, 0, -1.0, -249.0, -85.6, 1.000, -0.040, 2, 8000, 8, 'stopped_no_target', 'Onyxia S healer side 1 point 0: 2 slots, stand to point 88 yd, path-check ok'),
-- Hover point 4 SW (anchor 5) (-58.3 -189.0), breath 18596
(249, 10184, 5, -58.2509, -189.0200, 8, 'ranged', 18596, 17131, 0, 0, -28.0, -174.0, -88.3, 0.760, -0.650, 5, 8000, 1, 'stopped_no_target', 'Onyxia SW ranged side 0 point 0: 5 slots, stand to point 64 yd, path-check ok'),
(249, 10184, 5, -58.2509, -189.0200, 8, 'ranged', 18596, 17131, 0, 1, -26.0, -164.0, -87.2, 0.760, -0.650, 5, 8000, 1, 'stopped_no_target', 'Onyxia SW ranged side 0 point 1: 5 slots, stand to point 74 yd, path-check ok'),
(249, 10184, 5, -58.2509, -189.0200, 8, 'ranged', 18596, 17131, 1, 0, -80.0, -212.0, -82.5, 0.760, -0.650, 5, 8000, 1, 'stopped_no_target', 'Onyxia SW ranged side 1 point 0: 5 slots, stand to point 1 yd, path-check ok'),
(249, 10184, 5, -58.2509, -189.0200, 8, 'healer', 18596, 17131, 0, 0, -4.0, -170.0, -83.0, 0.760, -0.650, 5, 8000, 1, 'stopped_no_target', 'Onyxia SW healer side 0 point 0: 5 slots, stand to point 86 yd, path-check ok'),
(249, 10184, 5, -58.2509, -189.0200, 8, 'healer', 18596, 17131, 0, 1, 8.0, -204.0, -85.6, 0.760, -0.650, 4, 8000, 1, 'stopped_no_target', 'Onyxia SW healer side 0 point 1: 4 slots, stand to point 88 yd, path-check ok'),
(249, 10184, 5, -58.2509, -189.0200, 8, 'healer', 18596, 17131, 0, 2, 12.0, -172.0, -84.4, 0.760, -0.650, 2, 8000, 1, 'stopped_no_target', 'Onyxia SW healer side 0 point 2: 2 slots, stand to point 100 yd, path-check ok'),
(249, 10184, 5, -58.2509, -189.0200, 8, 'healer', 18596, 17131, 0, 3, 24.0, -204.0, -84.1, 0.760, -0.650, 2, 8000, 1, 'stopped_no_target', 'Onyxia SW healer side 0 point 3: 2 slots, stand to point 104 yd, path-check ok'),
(249, 10184, 5, -58.2509, -189.0200, 8, 'healer', 18596, 17131, 1, 0, -82.0, -222.0, -81.8, 0.760, -0.650, 3, 8000, 1, 'stopped_no_target', 'Onyxia SW healer side 1 point 0: 3 slots, stand to point 11 yd, path-check ok'),
-- Hover point 5 W (anchor 6) (-33.6 -182.7), breath 18609
(249, 10184, 6, -33.5561, -182.6820, 8, 'ranged', 18609, 17131, 0, 0, -2.0, -192.0, -86.6, 0.030, -1.000, 5, 8000, 2, 'stopped_no_target', 'Onyxia W ranged side 0 point 0: 5 slots, stand to point 81 yd, path-check ok'),
(249, 10184, 6, -33.5561, -182.6820, 8, 'ranged', 18609, 17131, 0, 1, 8.0, -192.0, -85.6, 0.030, -1.000, 5, 8000, 2, 'stopped_no_target', 'Onyxia W ranged side 0 point 1: 5 slots, stand to point 90 yd, path-check ok'),
(249, 10184, 6, -33.5561, -182.6820, 8, 'ranged', 18609, 17131, 1, 0, -66.0, -192.0, -84.1, 0.030, -1.000, 5, 8000, 2, 'stopped_no_target', 'Onyxia W ranged side 1 point 0: 5 slots, stand to point 24 yd, path-check ok'),
(249, 10184, 6, -33.5561, -182.6820, 8, 'healer', 18609, 17131, 0, 0, 18.0, -192.0, -84.6, 0.030, -1.000, 5, 8000, 2, 'stopped_no_target', 'Onyxia W healer side 0 point 0: 5 slots, stand to point 100 yd, path-check ok'),
(249, 10184, 6, -33.5561, -182.6820, 8, 'healer', 18609, 17131, 0, 1, 28.0, -192.0, -83.0, 0.030, -1.000, 5, 8000, 2, 'stopped_no_target', 'Onyxia W healer side 0 point 1: 5 slots, stand to point 110 yd, path-check ok'),
(249, 10184, 6, -33.5561, -182.6820, 8, 'healer', 18609, 17131, 0, 2, -2.0, -182.0, -86.0, -0.030, 1.000, 2, 8000, 2, 'stopped_no_target', 'Onyxia W healer side 0 point 2: 2 slots, stand to point 83 yd, path-check ok'),
(249, 10184, 6, -33.5561, -182.6820, 8, 'healer', 18609, 17131, 0, 3, 8.0, -182.0, -85.1, -0.030, 1.000, 2, 8000, 2, 'stopped_no_target', 'Onyxia W healer side 0 point 3: 2 slots, stand to point 93 yd, path-check ok'),
(249, 10184, 6, -33.5561, -182.6820, 8, 'healer', 18609, 17131, 1, 0, -76.0, -192.0, -82.6, 0.030, -1.000, 5, 8000, 2, 'stopped_no_target', 'Onyxia W healer side 1 point 0: 5 slots, stand to point 19 yd, path-check ok'),
-- Hover point 6 NW (anchor 7) (6.9 -180.2), breath 18584
(249, 10184, 7, 6.8951, -180.2460, 8, 'ranged', 18584, 17131, 0, 0, 8.0, -220.0, -85.4, -0.760, -0.650, 5, 8000, 3, 'stopped_no_target', 'Onyxia NW ranged side 0 point 0: 5 slots, stand to point 88 yd, path-check ok'),
(249, 10184, 7, 6.8951, -180.2460, 8, 'ranged', 18584, 17131, 0, 1, 24.0, -218.0, -84.2, -0.760, -0.650, 5, 8000, 3, 'stopped_no_target', 'Onyxia NW ranged side 0 point 1: 5 slots, stand to point 104 yd, path-check ok'),
(249, 10184, 7, 6.8951, -180.2460, 8, 'ranged', 18584, 17131, 1, 0, -46.0, -184.0, -85.9, -0.760, -0.650, 5, 8000, 3, 'stopped_no_target', 'Onyxia NW ranged side 1 point 0: 5 slots, stand to point 44 yd, path-check ok'),
(249, 10184, 7, 6.8951, -180.2460, 8, 'healer', 18584, 17131, 0, 0, 26.0, -228.0, -83.7, -0.760, -0.650, 5, 8000, 3, 'stopped_no_target', 'Onyxia NW healer side 0 point 0: 5 slots, stand to point 107 yd, path-check ok'),
(249, 10184, 7, 6.8951, -180.2460, 8, 'healer', 18584, 17131, 0, 1, 28.0, -238.0, -83.0, -0.760, -0.650, 4, 8000, 3, 'stopped_no_target', 'Onyxia NW healer side 0 point 1: 4 slots, stand to point 111 yd, path-check ok'),
(249, 10184, 7, 6.8951, -180.2460, 8, 'healer', 18584, 17131, 0, 2, 24.0, -208.0, -84.2, 0.760, 0.650, 2, 8000, 3, 'stopped_no_target', 'Onyxia NW healer side 0 point 2: 2 slots, stand to point 104 yd, path-check ok'),
(249, 10184, 7, 6.8951, -180.2460, 8, 'healer', 18584, 17131, 0, 3, -12.0, -250.0, -86.6, -0.760, -0.650, 2, 8000, 3, 'stopped_no_target', 'Onyxia NW healer side 0 point 3: 2 slots, stand to point 78 yd, path-check ok'),
(249, 10184, 7, 6.8951, -180.2460, 8, 'healer', 18584, 17131, 1, 0, -62.0, -186.0, -84.3, -0.760, -0.650, 3, 8000, 3, 'stopped_no_target', 'Onyxia NW healer side 1 point 0: 3 slots, stand to point 31 yd, path-check ok'),
-- Hover point 7 N (anchor 8) (22.9 -217.2), breath 17086
(249, 10184, 8, 22.8763, -217.1520, 8, 'ranged', 17086, 17131, 0, 0, 0.0, -248.0, -85.7, -1.000, 0.040, 4, 8000, 4, 'stopped_no_target', 'Onyxia N ranged side 0 point 0: 4 slots, stand to point 88 yd, path-check ok'),
(249, 10184, 8, 22.8763, -217.1520, 8, 'ranged', 17086, 17131, 1, 0, 4.0, -184.0, -85.8, -1.000, 0.040, 4, 8000, 4, 'stopped_no_target', 'Onyxia N ranged side 1 point 0: 4 slots, stand to point 89 yd, path-check ok'),
(249, 10184, 8, 22.8763, -217.1520, 8, 'ranged', 17086, 17131, 1, 1, 4.0, -174.0, -84.6, -1.000, 0.040, 4, 8000, 4, 'stopped_no_target', 'Onyxia N ranged side 1 point 1: 4 slots, stand to point 92 yd, path-check ok'),
(249, 10184, 8, 22.8763, -217.1520, 8, 'healer', 17086, 17131, 0, 0, 10.0, -248.0, -85.2, 1.000, -0.040, 2, 8000, 4, 'stopped_no_target', 'Onyxia N healer side 0 point 0: 2 slots, stand to point 97 yd, path-check ok'),
(249, 10184, 8, 22.8763, -217.1520, 8, 'healer', 17086, 17131, 1, 0, 18.0, -186.0, -84.6, 1.000, -0.040, 2, 8000, 4, 'stopped_no_target', 'Onyxia N healer side 1 point 0: 2 slots, stand to point 102 yd, path-check ok'),
(249, 10184, 8, 22.8763, -217.1520, 8, 'healer', 17086, 17131, 1, 1, -34.0, -184.0, -88.3, -1.000, 0.040, 3, 8000, 4, 'stopped_no_target', 'Onyxia N healer side 1 point 1: 3 slots, stand to point 54 yd, path-check ok');
