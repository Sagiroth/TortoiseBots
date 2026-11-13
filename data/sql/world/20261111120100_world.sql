-- Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834): r11-4, the spread row also names who drinks a
-- protection potion.
--
-- 24328 Corrupted Blood is shadow, cannot be dispelled, and is about two thirds of the raid's damage taken: a landing
-- is about 2000, and a Greater Shadow Protection Potion 13459 absorbs 1950-3250. The row already names the spell the
-- raid spreads for, so with AiPlayerbot.DungeonRunBossProtectionPotion = 1 its school (spell_template 24328 `school`
-- 5, shadow) picks the potion. `priority` was unused on spread rows; it is now a role mask: 1 tanks, 2 melee damage,
-- 4 ranged damage, 8 healers. 7 = tanks and both damage roles, no healer: healers keep the shared potion cooldown for
-- mana potions, used in 282 of 474 healer-fights. Every other spread row keeps priority 0 (nobody) and is inert.
--
-- Only `priority` and `comment` change; `param` stays 12 (the spacing), target_entry 0 and spell_id 24328, so a binary
-- from before this change reads the row exactly as before. Needs DungeonRunBossProtectionPotion = 1 (default 0).
--
-- Idempotent: the DELETE names this file's one (map, boss, kind, target, spell, band) key. Never edit this file once
-- applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'spread' AND `target_entry` = 0 AND `spell_id` = 24328 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'spread', 0, 24328, 7, 12, '', 0, 0, 0, 0, 0, 0, 'Hakkar: Corrupted Blood 24328 (shadow) hits 10 yd around a random attacker; ranged and healers keep 12 yd apart; tanks and damage dealers drink the shadow protection potion (priority 7)');
