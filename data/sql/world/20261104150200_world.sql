-- Boss tactics: AQ20 rows, step 2 (walk-30 fixes): the T2 burn_mana and T3 ranged_only rows. The kinds' C++ lands with
-- this migration, so the loader reads both rows from the first boot.
--
-- Every id checked in the core (tortoise-wow, sql/base + src/scripts):
--   Moam 15340: Drain Mana 25676 (boss_moam.cpp:65,209-215) casts 25671 on each target in its radius
--     (SpellEffects.cpp:3685-3689), which is how he fills up; Arcane Eruption 25672 fires at full mana outside the
--     stone phase (boss_moam.cpp:166-170,142-160). Up to four burners keep him below a full bar from 10 % mana.
--   Ayamiss 15369: 5 s into the fight she climbs 20 yd straight up (boss_ayamiss.cpp:193-205) and lands at 70 %
--     (:209-225); a melee bot cannot reach her in the air, so it is held under an 8 yd vertical gap and takes the
--     adds (kill_first 15555 larva, 15934 hornet).
--
-- Idempotent: each DELETE names one exact (map, boss, kind, target, spell) key, and this file's own two keys are the
-- only ones it writes. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 509 AND `boss_entry` = 15340 AND `kind` = 'burn_mana' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 509 AND `boss_entry` = 15369 AND `kind` = 'ranged_only' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `comment`) VALUES
(509, 15340, 'burn_mana', 0, 0, 4, 10, '', 'Moam: up to four burners keep him below full mana from 10 % (Drain Mana 25676 fills him, SpellEffects.cpp:3685-3689; Arcane Eruption 25672 at full, boss_moam.cpp:166-170)'),
(509, 15369, 'ranged_only', 0, 0, 0, 8, '', 'Ayamiss: melee bots are held off her while she stands 8 yd or more above them in the air (she climbs 20 yd at 5 s, boss_ayamiss.cpp:193-205) and take the larva and hornet adds');
