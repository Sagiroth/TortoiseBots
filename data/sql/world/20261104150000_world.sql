-- Boss tactics: AQ20 rows, step 1 (walk-30 fixes). Every kind below already has a
-- consumer in the module; rows for not-yet-built kinds (burn_mana, ranged_only)
-- go in a later migration so the loader never skips them.
--
-- Every id checked in the core (tortoise-wow, sql/base + src/scripts):
--   Kurinnaxx 15348: Mortal Wound 25646 (boss_kurinnaxx.cpp:61,120-126):
--     healing-taken aura 118, -10 %/stack, 10 stacks, duration index 8,
--     recast every 8-10 s. swap at 5 stacks between the tank and off-tank 1.
--   Ossirian 15339: Strength of Ossirian 25176 (boss_ossirian.cpp:95,178,252-258)
--     falls when a weakness lands (SpellWeakness, boss_ossirian.cpp:47-54:
--     25177/78/80/81/83; the header's 25171 is "Time Stop Visual DND" and is
--     unused by the crystal) and returns 45 s after the last weakness hit.
--     The crystal GO is 180619, type 10, script ossirian_crystal.
--   Ossirian: Enveloping Winds 25189 zeroes the stunned victim's threat for the
--     stun (boss_ossirian.cpp:131-139,278-295); off-tank 1 takes him back in 4 s.
--   Ossirian: Curse of Tongues 25195, dispel type 2 (curse).
--   Moam 15340: Mana Fiends 15527, 3 summoned each stone phase
--     (boss_moam.cpp:173-197), 13-14.6 k hp, 940-962 melee.
--   Ayamiss 15369: Poison Stinger 25748 (boss_ayamiss.cpp:295-301): its
--     preventionType is 2, not SILENCE (1), so the core cannot interrupt it
--     (SpellEffects.cpp:3389-3393) and the row is dropped for this round.
--   Ayamiss: Hive'Zara Hornet 15934 from a fed larva (boss_ayamiss.cpp:417),
--     47.1 k hp; the threat wipe at 70 % (boss_ayamiss.cpp:217-224) is taken by
--     the main tank in 4 s.
--   Entrance: Qiraji Gladiator pair 15324: the survivor gains Vengeance 25164
--     once one dies (ruins_of_ahnqiraj.cpp:636-640, instance flag); equalise,
--     hold at 5 %, burn.
--
DELETE FROM `ai_boss_tactic` WHERE `map` = 509 AND `boss_entry` = 15348 AND `kind` = 'swap_on_aura' AND `target_entry` = 0 AND `spell_id` = 25646 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 509 AND `boss_entry` = 15339 AND `kind` = 'use_object' AND `target_entry` = 180619 AND `spell_id` = 25176 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 509 AND `boss_entry` = 15339 AND `kind` = 'taunt_after_cast' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 509 AND `boss_entry` = 15339 AND `kind` = 'dispel_priority' AND `target_entry` = 0 AND `spell_id` = 25195 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 509 AND `boss_entry` = 15340 AND `kind` = 'kill_first' AND `target_entry` = 15527 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 509 AND `boss_entry` = 15369 AND `kind` = 'kill_first' AND `target_entry` = 15934 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 509 AND `boss_entry` = 15369 AND `kind` = 'taunt_after_cast' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 70;
DELETE FROM `ai_boss_tactic` WHERE `map` = 509 AND `boss_entry` = 15324 AND `kind` = 'linked_death' AND `target_entry` = 15324 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `hp_min`, `hp_max`, `comment`) VALUES
(509, 15348, 'swap_on_aura', 0, 25646, 2, 5, '', 0, 0, 100, 'Kurinnaxx: Mortal Wound 25646 (aura 118, -10 % healing per stack, recast every 8-10 s); swap at 5 stacks between the main tank and off-tank 1'),
(509, 15339, 'use_object', 180619, 25176, 0, 25, 'tank_anchor', 38, 0, 100, 'Ossirian: crystal 180619 on Strength 25176 or every 38 s, 7 s before the 45 s recast; free-DPS clicker; boss within 25 yd and still'),
(509, 15339, 'taunt_after_cast', 0, 0, 1, 4, 'until_tank', 0, 0, 100, 'Ossirian: Enveloping Winds 25189 zeroes the stunned tank''s threat; off-tank 1 takes him back within 4 s'),
(509, 15339, 'dispel_priority', 0, 25195, 1, 0, 'now', 0, 0, 100, 'Ossirian: Curse of Tongues 25195, curse, -75 % casting speed'),
(509, 15340, 'kill_first', 15527, 0, 1, 0, '', 0, 0, 100, 'Moam: Mana Fiend 15527, 3 summoned at each stone phase, 13-14.6 k hp, 940-962 melee'),
(509, 15369, 'kill_first', 15934, 0, 2, 0, '', 0, 0, 100, 'Ayamiss: Hive''Zara Hornet 15934 from a fed larva, 47.1 k hp'),
(509, 15369, 'taunt_after_cast', 0, 0, 0, 4, 'until_tank', 0, 0, 70, 'Ayamiss: the threat wipe at 70 %; the main tank takes her back within 4 s; band 0-70 % so the air phase (Taunt is melee range) cannot spend the row'),
(509, 15324, 'linked_death', 15324, 0, 1, 5.0, '', 7, 0, 100, 'Qiraji Gladiator pair: the survivor gains Vengeance 25164 once one dies; equalise, hold at 5 %, burn');
