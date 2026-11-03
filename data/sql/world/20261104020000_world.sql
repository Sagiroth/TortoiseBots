-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834), the fight's four rows.
--
-- Every id is from the core (src/scripts/dungeons/zulgurub/boss_hakkar.cpp, sql/base/tw_world_spell_template.sql):
--   pull_gate   24692 Hakkar Power is one stack per High Priest whose instance state is not DONE (instance_zulgurub
--               sets it on Hakkar's creation and on each priest's SetData), each stack +20 % damage and +10 % max
--               health, five stacks at most. A pull with a stack up is a wipe, so the run leader never starts one
--               while the boss wears it and ends the run after 30 s (priority 0 = no stack allowed, param = seconds).
--   seek_aura   24321 Poisonous Blood is what damages Hakkar under Blood Siphon 24324: a siphon target with the aura
--               deals 200/s to Hakkar for 8 s, one without it heals him 1000 hp/s. The only source is the Son of
--               Hakkar 11357's death spell 24320 (effect 27 persistent area aura, 8 yd, 3 s, effect trigger 24321),
--               cast by EventAI event 1135701 (death) through script 1135701. The row has the DPS stop the Son at
--               30 % (priority) while the raid gathers within 4 yd (param) at B (-11802, -1683, 52.9), the cycle
--               clock due 90 s after the engage and restarted by a 24324 sighting (cycle_spell/cycle_s), and the
--               dose window opens 80 s before due (window_s).
--   spread      24328 Corrupted Blood hits every enemy within 10 yd of a random attacker (the tank included), so the
--               ranged raid and the healers keep 12 yd apart (param) while Hakkar fights; the spell id documents only.
--   taunt_after_cast 24327 Cause Insanity charms his victim for 10 s and restores its threat afterwards. It is
--               instant, so there is no cast to see; the first off-tank (priority 1) takes Hakkar back while he is
--               on a non-tank, with the 10 s deadline (param) as the trigger's own edge.
-- The three Hakkar dispel_priority rows of 20261103020000_world.sql stay.
--
-- Idempotent: each DELETE names one exact (map, boss, kind, target, spell, health band) key, and this file's own
-- four keys are the only ones it writes. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'seek_aura' AND `target_entry` = 11357 AND `spell_id` = 24321 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'pull_gate' AND `target_entry` = 0 AND `spell_id` = 24692 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'spread' AND `target_entry` = 0 AND `spell_id` = 24328 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'taunt_after_cast' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'seek_aura', 11357, 24321, 30, 4.0, '', 80, 24324, 90, -11802.0, -1683.0, 52.9, 'Hakkar: the raid stands in a dying Son of Hakkar cloud (24320 -> 24321) before each 90 s Blood Siphon 24324'),
(309, 14834, 'pull_gate', 0, 24692, 0, 30, '', 0, 0, 0, 0, 0, 0, 'Hakkar: never pull while he wears Hakkar Power 24692 (a living High Priest); end the run after 30 s'),
(309, 14834, 'spread', 0, 24328, 0, 12, '', 0, 0, 0, 0, 0, 0, 'Hakkar: Corrupted Blood 24328 hits 10 yd around a random attacker; ranged and healers keep 12 yd apart'),
(309, 14834, 'taunt_after_cast', 0, 0, 1, 10, 'until_tank', 0, 0, 0, 0, 0, 0, 'Hakkar: Cause Insanity 24327 charms his victim (instant, no cast to see); tanks[1] takes him while he is on a non-tank');
