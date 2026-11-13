-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834) — lengthen the opening threat hold 5 s to 12 s.
--
-- Evidence (hakkar r11-2 D1): the first Cause Insanity 24327 comes at 17 s. With the 5 s hold the damage dealers have
-- 12-14 s and reach 1.44 times the off-tank, so the cast goes to a non-tank in 53-78 % of fights (walks 64-66).
-- With 12 s they have about 5-7 s, a ratio of about 0.7. Cost: 7 s of damage once, about 2 % of his health. Not
-- longer than 12 s: the hold must end before the first Corrupted Blood (15 s) so the opener is not a second change,
-- and a 17 s hold would put zero damage-dealer threat under a cast whose turn then depends on healing threat.
--
-- Idempotent: the DELETE names one exact key (map, boss, kind, target, spell, health band), and this file re-inserts
-- that key. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'dps_hold_after' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'dps_hold_after', 0, 0, 0, 12, 'engage', 0, 0, 0, 0, 0, 0, 'Hakkar: hold DPS 12 s from the first engagement so the off-tanks are ahead of the damage dealers when the first Cause Insanity 24327 lands at 17 s');
