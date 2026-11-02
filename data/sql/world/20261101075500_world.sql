-- Boss stand sets: the table only (no rows). Where bots of a role stand while a boss is at an anchor and,
-- for a windowed row, casting the row's window spell. Read once at startup (ai/playerbot/BossStandMgr.cpp,
-- LoadBossStands); `.bot run reloadplans` does not reload it, a restart does. The seat engine is
-- ai/playerbot/BossStandPolicy.h; rows for a boss come in that boss's own migration (Venoxis: 20261101080000).
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
--                 whichever comes first. A cast whose effect outlasts it (a sweep along a lane) would catch a
--                 bot that walked back the moment the cast ended.
--                 During the hold the caster reach does not move the bot (it may cast from its seat); a new
--                 window (the next cast, at any anchor) re-seats from that anchor.
-- A bot takes the side it stands on (it never crosses the lane for its point) and the nearest point of
-- it with a free seat; a full side sends it to the other side; a full table seats it nowhere and it
-- keeps the behaviour it had. No row applying changes nothing for any bot.
--
-- Idempotent: CREATE TABLE IF NOT EXISTS, so a server that already has the table (from an earlier test build)
-- keeps it and its rows. The core runs each migration file inside one transaction. Never edit this file once
-- applied; add a newer migration.

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
