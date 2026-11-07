-- Boss tactics: Jin'do the Hexxer (Zul'Gurub, map 309, creature 11380), t1105h (171 fights of walks 42-43).
--
--   taunt_after_cast  deleted, both rows: Jin'do is immune to taunts (creature_template 11380 immunity_flags 6 =
--                     TAUNT|MOD_STAT), and 0 of the walks' taunts on him landed. The spell-less Hex/charm row
--                     (20261105100000) and the Banish 24466 row (20261105233000) could never take him back; they only
--                     burnt the off-tank's taunt. Deleted by their exact keys; the two older files stay as shipped.
--   tank_ignore       new kind: while Jin'do fights, no tank picks, guards, engages or taunts a Sacrificed Troll 14826
--                     (763 HP); the damage dealers kill them through the normal kill logic. The tanks taunted trolls
--                     915 times while Jin'do sat on a damage dealer 55 % of the time. Read straight from the table
--                     (BossTacticTable::TankIgnoreBosses): target_entry the troll, every other column unused.
--   threat_successor  Hakkar's row shape (20261105210000): the damage dealers' threat cap is the assigned off-tanks'
--                     highest, so after a Hex, a tank death or a threat loss the off-tank inherits him, not a DPS.
-- Every other Jin'do row stays: the Shade's needs_aura gate and kill_first row, the Delusions dispel `never` row, the
-- Hex dispel row, and the ward and totem kill_first rows.
--
-- Idempotent: each DELETE names one exact (map, boss, kind, target, spell, health band) key, and this file's own two
-- rows are the only ones it writes. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'taunt_after_cast' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'taunt_after_cast' AND `target_entry` = 0 AND `spell_id` = 24466 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'tank_ignore' AND `target_entry` = 14826 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'threat_successor' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 11380, 'tank_ignore', 14826, 0, 0, 0, '', 0, 0, 0, 0, 0, 0, 'Jindo: the Sacrificed Trolls 14826 (763 HP) are the damage dealers kill; the tanks stay on Jin''do, never guard or taunt a troll'),
(309, 11380, 'threat_successor', 0, 0, 0, 0, '', 0, 0, 0, 0, 0, 0, 'Jindo: taunt-immune (immunity_flags 6) and Hex wipes the tank threat; the damage dealers keep below the off-tank so it inherits him');
