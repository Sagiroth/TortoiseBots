-- Onyxia's Lair (map 249), walk 2e: the post's arrival radius becomes 90.
--
-- Walk 2d evidence (a-2): Cindarion stood 41-60 yd from the post spot T instead of the 21-26 yd of
-- walk 2c, the post leashed eight times and still latched only at +107 s, so slot 1 held him for a
-- third of the fight and the other two reservations lapsed on grace. The radius is now the whole floor
-- the `anchor` row allows: the room centre (-22, -213.5) is 32.8 yd from T plus its 60 yd radius gives
-- 92.8, and the healer breath seats stand 70-77 yd from T, so 60 would still miss a Cindarion chasing a
-- seated healer. Before he lands he is unselectable, so the large radius cannot latch early. The
-- loader's cap goes 40 -> 100 on the same branch (the old loader drops this row, and with it the post).
-- This replaces the 20261110130000 tank_post row; the row is otherwise identical.
--
-- Idempotent: the DELETE removes only the (249, 10184) tank_post row. Never edit this file once
-- applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'tank_post';
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 10184, 'tank_post', 50143, 0, 1, 90, '', 41, 65, 0, 'hold_spot', 120, 0, 0, 8.0, -200.0, -85.8, 'Onyxia 65-41 %: slot 1 posts at T for Cindarion (hold_spot); radius 90 = the anchor''s whole floor (walk 2d a-2: he stood 41-60 yd from T, 8 leash walks, arrived at +107 s)');
