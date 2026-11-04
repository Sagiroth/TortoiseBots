-- Kurinnaxx (15348, Ruins of Ahn'Qiraj): the tank swap on Mortal Wound.
--
-- Mortal Wound 25646 is cast on the current victim 8-10 s into the fight and again every 8-10 s (boss_kurinnaxx.cpp
-- :61,:120-126). Its aura 118 cuts healing taken by 10 % per stack and stacks to 10, so one tank reaches -100 % in
-- 80-100 s. At 5 stacks the other tank taunts him off (the swap_on_aura consumer reads this row; rotation of two,
-- main tank then off-tank 1).
--
-- Idempotent: the DELETE names the one exact key, so a later row of the same kind (the AQ20 data migration) is
-- replaced rather than duplicated.

DELETE FROM `ai_boss_tactic` WHERE `map` = 509 AND `boss_entry` = 15348 AND `kind` = 'swap_on_aura';
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `bot_aura`, `tactic_mode`, `comment`) VALUES
(509, 15348, 'swap_on_aura', 0, 25646, 2, 5, 0, '', 'Mortal Wound 25646, aura 118 -10% healing per stack, stack 10, duration index 8, recast every 8-10 s (boss_kurinnaxx.cpp:61,120-126); swap at 5 stacks between the main tank and off-tank 1');
