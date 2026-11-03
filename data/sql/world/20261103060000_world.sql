-- Boss tactics: delete the two Zul'Gurub dps_hold_after `until_tank` rows an earlier build of
-- 20261103020000_world.sql inserted (Hakkar 14834's Cause Insanity 24327 and Mandokir 11382's Threatening Gaze
-- 24314) on servers that already ran that build. The core's AutoUpdater applies a migration by file name, so such a
-- server keeps the rows and never sees the corrected file; its live table would not match the code.
--
-- Both rows are spell-less (target_entry 0, spell_id 0), and a spell-less hold fires only its engage edge, once per
-- fight, so "until_tank" never recurred. Their triggers are auras on players (or an instant cast, Cause Insanity),
-- which the hold's cast/aura edge cannot see on the boss. Do not re-add without a trigger that recurs
-- (docs/play/boss-tactics.md, Known limits).
--
-- Idempotent: on a server that ran the corrected 20261103020000, or a fresh one, the DELETE matches nothing; it
-- touches no other row of either boss (any keyed dps_hold_after row keeps its spell id).

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14834, 11382) AND `kind` = 'dps_hold_after' AND `target_entry` = 0 AND `spell_id` = 0;
