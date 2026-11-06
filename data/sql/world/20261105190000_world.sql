-- Arlokk 14515 (boss_arlokk.cpp): prowlers 15101 spawn 2 per 5 s without end (alive cap 30), chase the Mark carrier and
-- wipe their own threat every 2 s; Vanish (ScriptedAI::EnterVanish) zeroes every threat, the reappearance casts
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14515 AND `kind` = 'kill_first' AND `target_entry` = 14515 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14515 AND `kind` = 'kill_first' AND `target_entry` = 15101 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14515 AND `kind` = 'taunt_after_cast' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14515 AND `kind` = 'dps_hold_after' AND `target_entry` = 0 AND `spell_id` = 24190 AND `hp_min` = 0 AND `hp_max` = 100;
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `hp_min`, `hp_max`, `comment`) VALUES
(309, 14515, 'kill_first', 14515, 0, 1, 0, '', 0, 0, 100, 'Arlokk: the boss first whenever she is targetable (she must die before the 2nd Vanish at ~145 s); explicit entry so the kill-first grid finds her after the threat wipe'),
(309, 14515, 'kill_first', 15101, 0, 2, 0, '', 0, 0, 100, 'Arlokk: Zulian Prowler 15101 only while she is vanished; also keeps auto CC off them (NoCcEntries)'),
(309, 14515, 'taunt_after_cast', 0, 0, 1, 3, 'until_tank', 0, 0, 100, 'Arlokk: Ambush (random target after Vanish) and Gouge 12540 (-80 % threat, main tank incapacitated); off-tank 1 takes her back within 3 s'),
(309, 14515, 'dps_hold_after', 0, 24190, 0, 2, 'until_tank', 0, 0, 100, 'Arlokk: panther transform 24190 marks the Ambush after each Vanish (all threat 0); DPS hold 2 s, up to 6 s while she is on a non-tank');
