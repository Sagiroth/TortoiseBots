-- Boss tactics: Hakkar the Soulflayer's (map 309, creature 14834) threat_successor row also carries his Cause
-- Insanity timer, read only with AiPlayerbot.DungeonRunThreatResetWindow.
--
--   cycle_spell 24327  Cause Insanity. The fight's charm scan sees the spell land, so the boss's own row names it
--                      rather than code: a row with cycle_spell 0 keeps today's successor cap and no window.
--   param 19           the earliest gap between CIs in seconds (the window's minimum).
--   window_s 26        the latest gap (the window's maximum; walk 61-62, raid-zg-walk62/whysiphon/citimer.out:
--                      first CI at 16.2-18.3 s, gaps p5 20.4 s, median 23.1 s, about 10 % double gaps of 40-48 s).
--   priority 95        a damage dealer at or above 95 % of the reset reference holds its damage on Hakkar (walk 62:
--                      49 of 83 non-tank turns were a DPS above the best remaining tank's threat); it resumes under 90 %.
--
-- The window is open when the time since the last charm lies in [k*19, k*26] s for k = 1, 2 or 3, which covers the
-- double gaps; it is open about 7 s of every 22.5 s. Every other column is the row 20261105210000_world.sql wrote.
--
-- Idempotent: the DELETE names the same (map, boss, kind, target, spell, health band) key, and this file's own row is
-- the only one it writes. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'threat_successor' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'threat_successor', 0, 0, 95, 19, '', 26, 24327, 0, 0, 0, 0, 'Hakkar: Cause Insanity (24327) can be timed from its last reset, 19-26 s apart; a damage dealer at 95 % of the best tank that does not hold him holds its damage around the expected reset');
