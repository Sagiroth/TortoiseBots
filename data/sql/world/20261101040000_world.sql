-- Boss tactics: a tank_hold row for the Mad Voidwalker's channel in Zul'Gurub (map 309).
--
-- kind tank_hold (docs/play/boss-tactics.md): a tank may stay in this caster's avoid_cast blast while the caster is on
-- a non-tank, so it can take or keep the caster. Without the row a tank only holds a caster that is attacking the tank
-- itself, so every other avoid_cast blast clears the tank out like anyone else (round 1 held EVERY tank in EVERY
-- avoid_cast blast, which walked tanks into the Gurubashi Bat Rider's 10 yd explosion for nothing). The row is opt-in
-- per caster: the caster is named by `boss_entry` (with `target_entry` 0), and only the tank whose own current target
-- is that caster keeps it, and only while a non-tank holds its aggro.
--
-- Mad Voidwalker 15146 (Zul'Gurub, Venoxis leg): a dying Mad Servant leaves a Portal of Madness that summons two
-- Voidwalkers. EventAI 1514601 channels Consuming Shadows 24614 (5 yd, 400 shadow a second) on its target within 5 yd;
-- EventAI 1514602 casts instant Shadow Shock 24616 (15 yd, level-scaled ~1000-1200 shadow) whenever its target is 6-40
-- yd away. Untanked (walk 20), the target walked out of the channel and the shock killed most of the raid. With this
-- row the tank that has or picks the Voidwalker as its target stays in and takes it, and the loose-add choice prefers
-- a tank_hold caster that is on a non-tank; until the tank has aggro the healer still walks out, so a Shadow Shock can
-- still land in that window (no taunt is added).
--
-- The Gurubashi Bat Rider 14750 (20261030160000_world.sql) gets NO tank_hold row: its Unstable Concoction 24024 is a
-- 10 yd self blast it casts once before it dies, and holding a tank in it only kills the tank.
--
-- Idempotent: the DELETE removes only this row's key, then it goes back in. Never edit this file once applied; add a
-- newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 15146 AND `kind` = 'tank_hold' AND `target_entry` = 0 AND `spell_id` = 0;
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `comment`) VALUES
(309, 15146, 'tank_hold', 0, 0, 0, 0, 'Mad Voidwalker: a tank may hold it in melee while Consuming Shadows 24614 is on a non-tank; once the tank has it within 6 yd it channels instead of casting Shadow Shock 24616');
