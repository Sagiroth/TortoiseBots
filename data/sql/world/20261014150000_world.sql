-- Dungeon plans for the opt-in run leader (`.bot run`, docs/guides/dungeon-run-leader.md).
--
-- Schema only. The rows come from turtle-agent's plan exporter (its dungeons/<name>.json,
-- plan v1, is the single source) in later migrations of their own. Read once at startup
-- into a cache (ai/playerbot/DungeonPlanMgr.cpp); nothing reads these tables per tick.
--
-- ai_dungeon_plan  one row per plan: the name `.bot run start <plan>` takes and its map.
-- ai_dungeon_leg   one row per leg: a name for the status line (the plan's legs[i].name).
-- ai_dungeon_step  the steps, run in (leg, seq) order. One row is one thing to do:
--   kind         x,y,z                 other columns
--   move         the point to walk to  -
--   move_direct  the point             - (straight walk where the navmesh has a gap)
--   pull         centre of the pack    radius (0: 15 yd), entries, flags 1 ambush / 2 alliance / 4 horde
--   boss         where the boss stands entry (the boss), entries (killed first, in order), radius (0: 30 yd), flags 2/4
--   loot         near point            entry (chest or corpse), item (the item that must end in the bags; 0: none)
--   open         near point            entry (the object), item (the item the lock needs; 0: none)
--   use_item     -                     entry (the item to use), item (the item it must make; 0: none)
--   wait_object  near point            entry (the object), param (0 ready, 1 active, 2 active_alt), timeout_s
--   wait_clear   centre                radius (yd: no living hostile in it), timeout_s
--   rest         -                     - (a rest gate even when nobody is low)
--   ritual       near point            entry (the object) - loaded, not run yet
-- timeout_s: 0 is the default 60 s; at most 600. Every step also has the run leader's own ceiling.
--
-- Idempotent: CREATE TABLE IF NOT EXISTS only. Never edit this file once applied; add a newer
-- migration.

CREATE TABLE IF NOT EXISTS `ai_dungeon_plan` (
  `plan` varchar(32) NOT NULL,
  `map` int unsigned NOT NULL,
  `source` varchar(64) NOT NULL DEFAULT '',
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`plan`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_dungeon_leg` (
  `plan` varchar(32) NOT NULL,
  `leg` tinyint unsigned NOT NULL,
  `name` varchar(32) NOT NULL DEFAULT '',
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`plan`, `leg`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

CREATE TABLE IF NOT EXISTS `ai_dungeon_step` (
  `plan` varchar(32) NOT NULL,
  `leg` tinyint unsigned NOT NULL,
  `seq` smallint unsigned NOT NULL,
  `kind` varchar(16) NOT NULL,
  `entry` int unsigned NOT NULL DEFAULT 0,
  `x` float NOT NULL DEFAULT 0,
  `y` float NOT NULL DEFAULT 0,
  `z` float NOT NULL DEFAULT 0,
  `radius` float NOT NULL DEFAULT 0,
  `item` int unsigned NOT NULL DEFAULT 0,
  `param` int unsigned NOT NULL DEFAULT 0,
  `flags` int unsigned NOT NULL DEFAULT 0,
  `timeout_s` smallint unsigned NOT NULL DEFAULT 0,
  `entries` varchar(255) NOT NULL DEFAULT '',
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`plan`, `leg`, `seq`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
