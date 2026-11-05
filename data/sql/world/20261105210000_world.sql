-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834) asks for a threat successor.
--
--   threat_successor  Cause Insanity 24327 charms his victim and the core's charm handler deletes the victim's threat;
--                     Hakkar is also immune to taunts (creature_template 14834 immunity_flags 6), so no tank can take
--                     him back. Without a row the damage dealers measure their threat against the highest tank and a
--                     threat reset hands Hakkar to a damage dealer; the row makes their cap the assigned off-tanks'
--                     highest threat, so the off-tank is second and takes him. The kind is read straight from this
--                     table by ThreatValue::GetTankThreat (ai/playerbot/ThreatCapPolicy.h), so it takes no other
--                     column: target_entry 0 (Hakkar himself), spell_id, priority, param and the cycle/spot columns
--                     unused.
-- The three Hakkar dispel_priority rows, the seek_aura, spread, pull_gate and taunt_after_cast rows stay.
--
-- Idempotent: the DELETE names one exact (map, boss, kind, target, spell, health band) key, and this file's own row is
-- the only one it writes. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'threat_successor' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'threat_successor', 0, 0, 0, 0, '', 0, 0, 0, 0, 0, 0, 'Hakkar: Cause Insanity 24327 zeroes the main tank and taunts cannot land; the damage dealers keep below the off-tank so a threat reset hands him to it');
