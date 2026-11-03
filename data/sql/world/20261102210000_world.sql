-- Boss tactics: the Thekal die-together group's release burn order, a 2 % hold, and Lor'Khan's Greater Heal interrupt.
--
-- `burn_order` (new, empty for every other kind): the group's members in the order the raid kills them once the
-- linked_death hold is released, a comma list of creature entries. The Thekal trio raises each other: the chosen
-- raiser is the first living of Lor'Khan -> Zath -> Thekal (instance_zulgurub.cpp Thekal_GetUnitThatCanRez), and a
-- corpse whose raiser dies before the 7 s check gets an 8 s fallback (boss_thekal.cpp RESSURECT_TIMER,
-- HandleWaitRez). Killing Lor'Khan, then Zath, then Thekal makes every death kill the next corpse's raiser, so the
-- window is the 15 s fallback; an arbitrary order can leave the first corpse's raiser alive and gives only 7 s.
-- Every free DPS bot burns the first living member of the list, then the next (PickLinkedDeathBurn).
--
-- The hold is 2 %: the trio's pool is 325 540 hp and walk 27 measured 2.2-3.0 k raid dps, so the release burst is
-- about 6.5 k hp / 2-3 s, inside either window (3 % was 3-5 s and left less margin on the 7 s one).
--
-- Lorkhan's Greater Heal 24208 is interruptible (spell_template interruptFlags 15, castingTimeIndex 15): the heal on
-- the lowest friendly under 9500 hp keeps the last few percent of a member alive every 12 s and makes staggered
-- kills worse (boss_thekal.cpp).
--
-- Idempotent: the column is added through information_schema + PREPARE (MySQL 8 and MariaDB), and both DELETEs are
-- scoped to the exact keys re-inserted. Never edit this file once applied; add a newer migration.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_boss_tactic` ADD COLUMN `burn_order` varchar(96) NOT NULL DEFAULT '''' AFTER `param`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_tactic' AND COLUMN_NAME = 'burn_order');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14599) AND `kind` = 'linked_death';
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `comment`) VALUES
(309, 14599, 'linked_death', 11347, 0, 1, 2.0, '11347,11348,14599', 'Thekal: Lor''Khan (raised by Zath, then Thekal) is the first burn; the hold is 2 %, the release burst about 2-3 s'),
(309, 14599, 'linked_death', 11348, 0, 1, 2.0, '11347,11348,14599', 'Thekal: Zath (raised by Thekal) is the second burn; killing him kills Lor''Khan''s raiser before its 7 s check');

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (11347) AND `kind` = 'interrupt' AND `target_entry` = 0 AND `spell_id` = 24208;
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `comment`) VALUES
(309, 11347, 'interrupt', 0, 24208, 0, 0, '', 'Zealot Lor''Khan: Greater Heal on the lowest friendly under 9500 hp every 12 s keeps a staggered member alive');
