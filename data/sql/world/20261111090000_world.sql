-- Onyxia (map 249, creature 10184), walk 2d: hold DPS 8 s from the first engagement.
--
-- Walk 2c b-2/b-3/b-4: a damage dealer passed the tanks inside 8 s (b-2: a mage at 1,561 threat
-- at +8 s), so she walked into the raid at (-55, -210); Flame Breath 13 and Cleave 12 kills wiped
-- the pull at 67 %. Mode engage answers the pull only, once per pull; healers keep healing.
--
-- Idempotent: the DELETE names only the (249, 10184) dps_hold_after row. Never edit once applied.

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'dps_hold_after';
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 10184, 'dps_hold_after', 0, 0, 0, 8, '', 0, 100, 0, 'engage', 0, 0, 0, 0, 0, 0, 'Onyxia: hold DPS 8 s from the first engagement so the tanks hold her where they take her; walk 2c b-2: a mage passed the tanks at +8 s, she walked into the raid at (-55, -210) and Flame Breath and Cleave killed 25 (P1 wipe at 67 %)');
