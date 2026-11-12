-- Onyxia's Lair (map 249), walk 2h: the main tank holds Onyxia herself in P3, the north Hatcher takes
-- slot 1, and Flame Breath/Cleave get a frontal footprint. Also deletes the unused tank_post on 10184.
--
-- Walk 2g evidence (four P3 pulls, 109 deaths): Onyxia killed 47 bots (Flame Breath 17, Cleave 15; 28 of those
-- 32 are non-tanks) and nobody holds her: the slot-0 tank targeted a whelp in 42 % of its P3 frames, because the
-- tank pick ranks loose units by distance. She is fought at her landing point (-9, -213) with the raid all round
-- her and she had no `behind` row. Eight Hatchers spawned in P3 and only 4 had a post (the rows name slots 2 and
-- 3; the live tanks at the landings were 2, 4, 3, 3), so the north Hatcher row moves from slot 3 to slot 1: the
-- 50143 post keeps slot 1 in the 41-65 band and the north row carries 0-40, so the two slot-1 bands never overlap.
--
-- Spot E (20.0, -218.0, -85.3) is floor: offline path-check complete from the landing point (-9, -213, -88.5) at
-- 29.5 yd, from the room centre C (-22, -213.5) 42.4 yd, from T (8, -200) 21.7 yd, from S' 56.4 yd and from N'
-- 59.9 yd; the navmesh ends the path at (20.0, -218.0, -84.7), 0.6 yd short, and the point stands 71.8 yd from
-- the nearest whelp egg. The slot-0 row needs AiPlayerbot.DungeonRunTankPostMain = 1.
--
-- The `behind` row names Flame Breath 18435 (Cleave 19983 is melee, the same front) and arms only in P3 (band
-- 0-40): she turns all the time in the air, and P1 works. The 20 yd to 40 yd band clears the front for the
-- non-tanks while the main tank holds her facing east.
--
-- Idempotent: the DELETEs remove only the (249, 10184) tank_post on herself and every (249, 10184) `behind` row
-- this file replaces; the slot-1 50143 and the two 50144 posts stay. Never edit once applied; add a newer migration.

UPDATE `ai_boss_tactic` SET `priority` = 1, `comment` = 'Onyxia P3: slot 1 holds the north Hatcher (walk 2g: 4 of 8 Hatchers had a post; live tanks at the landings 2, 4, 3, 3)' WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'tank_post' AND `target_entry` = 50144 AND `hp_max` = 40;
UPDATE `ai_boss_tactic` SET `priority` = 1 WHERE `map` = 249 AND `boss_entry` = 50144 AND `kind` = 'taunt_after_cast';
UPDATE `ai_boss_tactic` SET `priority` = 2 WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'taunt_after_cast' AND `target_entry` = 50144;

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'tank_post' AND `target_entry` = 10184;
DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'behind';

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `burn_order`, `hp_min`, `hp_max`, `bot_aura`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(249, 10184, 'tank_post', 10184, 0, 0, 90, '', 0, 40, 0, 'hold_spot', 120, 0, 0, 20.0, -218.0, -85.3, 'Onyxia P3: slot 0 holds her at the east spot E, facing east (walk 2g: the main tank on a whelp in 42 % of P3 frames, 28 non-tank deaths to Flame Breath and Cleave; needs AiPlayerbot.DungeonRunTankPostMain)'),
(249, 10184, 'behind', 0, 18435, 0, 40, '', 0, 40, 0, '', 0, 0, 0, 0, 0, 0, 'Onyxia P3: Flame Breath 18435 and Cleave 19983 are frontal; non-tanks leave her front within 40 yd (P3 only: she turns all the time in the air)');
