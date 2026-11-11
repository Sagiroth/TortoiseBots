-- Onyxia's Lair (map 249), walk 2c: the post radius becomes 40 and melee leave her while she hovers.
--
-- Walk 2b evidence: slot 1's post at T latched 44-104 s late (radius 25; he landed 21-26 yd from T
-- and stood 26-31 yd off it), 27-38 % of melee frames chased airborne Onyxia to her hover point
-- (a breath lane's end), and threat-wiped or in-combat bots lost their seats. So the tank_post
-- arrival radius goes 25 -> 40 (the loader's maximum), and a ranged_only row (gap 10 yd) sends
-- melee and tanks to Cindarion and the whelps while she hovers (needs AiPlayerbot.DungeonRunRangedOnly = 1).
-- This replaces the 20261110120200 tank_post row.
--
-- Idempotent: the two DELETEs remove only the (249, 10184) tank_post and ranged_only rows. Never
-- edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'tank_post';
DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'ranged_only';
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 10184, 'tank_post', 50143, 0, 1, 40, '', 41, 65, 0, 'hold_spot', 120, 0, 0, 8.0, -200.0, -85.8, 'Onyxia 65-41 %: slot 1 posts at T for Cindarion (hold_spot); radius 40: walk 2b he landed 21-26 yd from T and stood 26-31 yd off it, so 25 latched 44-104 s late'),
(249, 10184, 'ranged_only', 0, 0, 0, 10, '', 0, 100, 0, '', 0, 0, 0, 0, 0, 0, 'Onyxia hovering (z -65, floor -86..-88): melee and tanks leave her and take Cindarion and the whelps; walk 2b: 27-38 % of melee frames chased her hover point, a breath lane end');
