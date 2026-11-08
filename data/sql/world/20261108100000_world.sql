-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834) — the pulled Son of Hakkar is burned on
-- arrival. Walk 53 (22 fights, 0 kills, 494 deaths, 51 walk-out backs):
--
--   The walk back lands at due 55-63 s. Pre-damage stops at the hold percent (30 + 5) and the gather starts only
--   there; it waits up to 8 s, and the forced burn began only at due 45 s. Back -> Son death was a median 31 s (p25
--   25, p75 36), and 27 of the 34 SonDeath rows read due_in 15-45. A Son of Hakkar (16,786 HP, 5,721 armor) therefore
--   had half a minute on the raid: its melee hits are 1.6-2.9k, and a cloth damage dealer has about 3.7k HP. The
--   pulled Son landed on a non-tank after 27 of 37 backs.
--
--   Holding a Son for its dose is not needed. Poisonous Blood 24321 lasts 90 s (durationIndex 23), the Blood Siphon
--   strips only the doses present when its 8 s aura ends (24324 durationIndex 31; SpellAuras.cpp:1875-1878), and the
--   poison cloud 24320 lasts 3 s (durationIndex 27). So a Son killed any time from 11 s after one siphon doses the
--   next: at most 79 s of lead (90 - 8 - 3). A 75 s kill lead forces the burn as soon as the pulled Son is engaged
--   (SeekLateEngagedBurnWithin), and the forced burn also marks the gather ready (SeekGatherReady), so the hold
--   percent and the 8 s gather wait drop out. No C++ change: the row's existing kill lead is the whole fix.
--
--   Documented limit: the forced burn skips the 4 yd gather, so the cloud lands where the waiting off-tank holds the
--   Son, inside the tanks' circle; the walk measures raid_in_cloud as its guardrail.
--
-- Idempotent: the DELETE names one exact (map, boss, kind, target, spell, health band) key, and this file's own row is
-- the only one it writes. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'seek_pull' AND `target_entry` = 11357 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'seek_pull', 11357, 0, 3, 75, '', 60, 0, 0, 0, 0, 0, 'Hakkar: up to 3 walk-outs a Blood Siphon cycle, none with less than 60 s left; an engaged Son of Hakkar is burned at once (75 s lead: Poisonous Blood lasts 90 s and the siphon strips it only at its own end)');
