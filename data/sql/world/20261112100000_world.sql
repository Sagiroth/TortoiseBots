-- Onyxia's Lair (map 249), walk 2g: the two P3 Hatchers 50144 get a post each, slots 2 and 3.
--
-- Walk 2f a-2 reached P3 with one post only (slot 1 on Cindarion) and no Hatcher was taunted all phase: 9 of the
-- P3 deaths went to the two 50144s, and each Hatcher's victim was a tank in 29 % of the frames. Both spawn points
-- path-check complete offline, but they stand 8.6 and 7.6 yd from the nearest whelp egg (4 eggs inside 14 yd each),
-- and a hold_spot post drags its subject back to the spot, so the spots move 12 yd toward the room centre C
-- (-22.0, -213.5): S' (-29.0, -245.7, -88.5) and N' (-29.0, -183.8, -88.4), 19.3 and 18.6 yd from the nearest egg,
-- path complete from T (58.9 and 40.5 yd) and from each spawn point (12.0 yd). The health bands are 0-41 and 0-40:
-- the loader refuses a second row whose kind, target, spell and band all equal a loaded one's, and both posts arm
-- in P3 (below 41 %), so the north row carries the narrower band only to stay loadable.
--
-- Idempotent: the DELETE removes only the two (249, 10184) tank_post rows for 50144, so the slot-1 row on 50143
-- (20261111160100_world.sql) stays. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'tank_post' AND `target_entry` = 50144;
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 10184, 'tank_post', 50144, 0, 2, 90, '', 0, 41, 0, 'hold_spot', 120, 0, 0, -29.0, -245.7, -88.5, 'Onyxia P3: slot 2 holds the south Hatcher 12 yd off its spawn point, clear of the eggs (walk 2f a-2: no Hatcher taunt, 9 deaths to Hatchers)'),
(249, 10184, 'tank_post', 50144, 0, 3, 90, '', 0, 40, 0, 'hold_spot', 120, 0, 0, -29.0, -183.8, -88.4, 'Onyxia P3: slot 3 holds the north Hatcher; band 0-40 only so the loader keeps both rows');
