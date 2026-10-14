-- Boss tactics: per-boss rows for the opt-in "boss tactics" strategy (P1).
--
-- Table format (docs/guides/boss-tactics.md): one row says what bots do while a
-- creature with `boss_entry` attacks them on `map`. Read once at startup into a
-- cache (ai/playerbot/BossTacticsMgr.cpp); nothing reads it per tick.
--   kind          interrupt | kill_first | behind
--   target_entry  the creature the row is about; 0 = the boss itself
--   spell_id      interrupt: the spell to interrupt
--   priority      kill_first: lower is killed first
--   param         behind: yards around the boss in which its front is kept clear
--
-- First content: Uldaman (map 70) up to Ironaya, plus the Obsidian Sentinel. Every
-- creature and spell below was read from Turtle's own world data (tortoise-wow
-- sql/base: creature_template, creature_spells, creature_ai_events/scripts,
-- spell_template; src/scripts/dungeons/uldaman); none of it comes from memory or
-- from another core's tables.
--
-- Idempotent: the DELETE removes only map 70 rows, then they go back in. The core
-- runs each migration file inside one transaction. Never edit this file once
-- applied; add a newer migration.

CREATE TABLE IF NOT EXISTS `ai_boss_tactic` (
  `map` int unsigned NOT NULL,
  `boss_entry` int unsigned NOT NULL,
  `kind` varchar(16) NOT NULL,
  `target_entry` int unsigned NOT NULL DEFAULT 0,
  `spell_id` int unsigned NOT NULL DEFAULT 0,
  `priority` tinyint unsigned NOT NULL DEFAULT 0,
  `param` float NOT NULL DEFAULT 0,
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

DELETE FROM `ai_boss_tactic` WHERE `map` = 70;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `comment`) VALUES
-- Revelosh (6910): spell list 69100 casts Chain Lightning 16006 (184 base damage, 8-13 s
-- first, then every 18-25 s) and a Lightning Bolt 15801 filler every 2 s; creature_template
-- spell_id1 is Chain Lightning 15117. All are casts that can be interrupted. Only Chain
-- Lightning is listed, so interrupts are not burnt on the filler bolt.
(70, 6910, 'interrupt', 0, 16006, 0, 0, 'Revelosh: Chain Lightning (spell list 69100)'),
(70, 6910, 'interrupt', 0, 15117, 0, 0, 'Revelosh: Chain Lightning (creature_template spell_id1)'),
-- The Lost Dwarves (6906 Baelog, 6907 Eric "The Swift", 6908 Olaf) fight together; the same
-- three rows sit under each, so whichever dwarf is pulled first sets the order. Baelog is the
-- only one with a ranged attack (Shoot 15620, range index 54) and he runs at 15% health
-- (event 690601), so he goes first; Eric opens with Rushing Charge 6268; Olaf has the most
-- health (5867 against 5231 and 5029) and the Shield Slam stun 8242, so he goes last.
(70, 6906, 'kill_first', 6906, 0, 1, 0, 'Lost Dwarves: Baelog first (ranged, flees at 15%)'),
(70, 6906, 'kill_first', 6907, 0, 2, 0, 'Lost Dwarves: Eric second'),
(70, 6906, 'kill_first', 6908, 0, 3, 0, 'Lost Dwarves: Olaf last'),
(70, 6907, 'kill_first', 6906, 0, 1, 0, 'Lost Dwarves: Baelog first (ranged, flees at 15%)'),
(70, 6907, 'kill_first', 6907, 0, 2, 0, 'Lost Dwarves: Eric second'),
(70, 6907, 'kill_first', 6908, 0, 3, 0, 'Lost Dwarves: Olaf last'),
(70, 6908, 'kill_first', 6906, 0, 1, 0, 'Lost Dwarves: Baelog first (ranged, flees at 15%)'),
(70, 6908, 'kill_first', 6907, 0, 2, 0, 'Lost Dwarves: Eric second'),
(70, 6908, 'kill_first', 6908, 0, 3, 0, 'Lost Dwarves: Olaf last'),
-- Ironaya (7228, script boss_ironaya): Arcing Smash 8374 every 13 s is a cone attack in front
-- of her (effect target 24, radius index 8, in the script's cast on herself). Everybody but
-- the tank keeps out of her front. Radius index 8 is the War Stomp 11876 radius (8 yd by its
-- tooltip; the DBC is not readable here), plus 2 yd. Her Knock Away 10101 under 50% health
-- and the threat wipe that goes with it are not handled yet.
(70, 7228, 'behind', 0, 8374, 0, 10, 'Ironaya: Arcing Smash cone, stay out of her front'),
-- Obsidian Sentinel (7023, EventAI): at 80/60/40/20% health it casts Summon Obsidian Shard
-- 10061 (creature 7209) and Splintered Obsidian 10072 on itself (events 702301-702304). The
-- shards are the first target.
(70, 7023, 'kill_first', 7209, 0, 1, 0, 'Obsidian Sentinel: Obsidian Shards first');
