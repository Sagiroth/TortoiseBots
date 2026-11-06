-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834) — keep the fight on his platform.
--
-- His script evades him the moment his z leaves 45.8-57.28 (src/scripts/dungeons/zulgurub/boss_hakkar.cpp:140-145).
-- He spawns at -11787.4 -1649.34 z 54.10 (tw_world creature guid 49678); the platform is flat for about 35-40 yd and
-- slopes away to the SW, where the z 45.8 line runs 45-50 yd from the spawn. The raid stood on that slope (the seek
-- spot 37 yd SW), so every target swap carried him over the line and he reset at full health.
--
--   anchor      While Hakkar fights, every boss/raid movement destination of the bots stays inside a circle around
--               his spawn: spot_x/y/z the centre, param the radius for everyone (30 yd), priority the tanks' radius
--               (25 yd). target_entry, spell_id, the mode word and the cycle columns are unused (0 / '').
--   seek_aura   The same row as 20261104020000_world.sql with the spot moved from (-11802, -1683, 52.9), 37 yd SW of
--               his spawn, to (-11787.4, -1664.3, 54.0), 15 yd S of it on the flat top.
-- The other Hakkar rows (dispel_priority, pull_gate, spread, taunt_after_cast, threat_successor, dps_hold_after) stay.
--
-- Idempotent: each DELETE names one exact (map, boss, kind, target, spell, health band) key, and this file re-inserts
-- both. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'anchor' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'seek_aura' AND `target_entry` = 11357 AND `spell_id` = 24321 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'anchor', 0, 0, 25, 30, '', 0, 0, 0, -11787.4, -1649.34, 54.1, 'Hakkar: his script evades him below z 45.8 (45-50 yd SW of his spawn); every boss/raid move stays within 30 yd of the spawn, tanks 25 yd'),
(309, 14834, 'seek_aura', 11357, 24321, 30, 4.0, '', 80, 24324, 90, -11787.4, -1664.3, 54.0, 'Hakkar: the raid stands in a dying Son of Hakkar cloud (24320 -> 24321) before each 90 s Blood Siphon 24324, held 15 yd S of his spawn');
