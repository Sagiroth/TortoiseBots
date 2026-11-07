-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834) — one Son pulled and killed in every
-- Blood Siphon cycle. Walk 51 (40 fights, 159 siphon cycles, rows deduplicated):
--
--   81 cycles undosed. In 30 cycles (16 undosed) no walk-out started at all and no row said why: both Sons (guid low
--   49033 / 49034) sat in walkedSons for the rest of the fight after two failed walk-outs.
--   Undosed first outcomes: 11 back, 14 timeout (13 with shot=0, the puller already 14-26 yd from the Son), 8 threat
--   aborts 0-3 s after the start, 7 no-stand aborts, 3 cast/move, 1 victim, 1 dead, 4 late starts with no end.
--   28 cycles (14 undosed) only logged `walk-out no stand`: the scan searched one ray at the non-hunter reach (28 yd).
--   Start -> back median 16 s (p90 22); back -> Son death median 27 s (p75 31, p90 40). Starts after -60 s and backs
--   after -45 s mostly did not kill before the siphon; the late-engaged forced burn started only at 30 s.
--
--   seek_pull   One row per seek source. The seek walk-out reads it straight from the table
--               (BossTacticTable::SeekPullRow) when AiPlayerbot.DungeonRunSeekPullCycle is on:
--                 target_entry  the seek source (the Son of Hakkar 11357; must be non-zero)
--                 priority      most walk-outs started in one Blood Siphon cycle (1-5)
--                 param         kill lead in seconds: the forced burn of an engaged source starts this long before
--                               the cycle spell is due (10-85)
--                 window_s      last start in seconds: no walk-out, first or retry, starts with less than this left
--                               to the due time (10-85)
--               spell_id 0, no mode word, no spot, no cycle columns, no gate.
--   The row's values: 3 walk-outs a cycle (a threat abort at 0-3 s and a no-stand abort at 7-23 s left enough time
--   for another bot), none with less than 60 s left (of the 22 cycles whose first walk-out started later, 17 went
--   undosed; of the 79 that started earlier, 34 did), a 45 s kill lead (a burn from 45 s kills about 18 s before
--   the siphon, and no kill earlier than that leaves the Son's 60 s respawn ahead of the next cycle's walk-out).
-- The seek_aura, anchor, pull_gate, spread, dps_hold_after, dispel_priority and threat_successor rows stay.
--
-- Idempotent: the DELETE names one exact (map, boss, kind, target, spell, health band) key, and this file's own row is
-- the only one it writes. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'seek_pull' AND `target_entry` = 11357 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'seek_pull', 11357, 0, 3, 45, '', 60, 0, 0, 0, 0, 0, 'Hakkar: up to 3 walk-outs a Blood Siphon cycle, none with less than 60 s left; an engaged Son of Hakkar is burned from 45 s before the siphon');
