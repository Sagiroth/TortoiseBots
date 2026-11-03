-- Boss tactics: pace the Mad Servant 15111 kills in Zul'Gurub (map 309) with two hold_kill rows, so at most one pair
-- of Mad Voidwalkers 15146 is alive at a time.
--
-- A dying Mad Servant casts Portal of Madness 24621 at its own corpse (creature_ai_events 1511103, event 6); the
-- portal's 1 s then every 30 s event (1514101) casts 24620, an aura whose 24622 summons two Mad Voidwalkers at the
-- same spot, 11 s after the death. Nothing puts them on a tank: they wake by proximity aggro. Their 1514602 event
-- casts instant Shadow Shock 24616 (15 yd, level-scaled about 1 000-1 200 shadow) whenever their target is 6-40 yd
-- away, so every Voidwalker shocks everyone near itself. Walk 21: D's four Servants died within 16 s, eight
-- Voidwalkers spawned together inside the raid and 7 bots died in the next 2 s; every fight with at most 4 alive went
-- well, every fight with 6 to 8 alive was a wipe or near wipe.
--
-- hold_kill (docs/play/boss-tactics.md): DPS bots leave `boss_entry` alone while more than `priority` living units of
-- `target_entry` are near, so `priority` is the budget. Budget 0 here: no DPS on a Mad Servant while a Mad Voidwalker
-- (15146) or a Portal of Madness (15141) lives, so Servants die a pair of Voidwalkers at a time; budget 2 is the
-- fallback if the roughly 230 s the pacing costs drain the healers (allow when at most 2 live and no portal).
--
-- The portal 15141 is unselectable (unit_flags 33555200) and never attacks, so the code counts the blockers with a
-- grid search, never from the bots' attacker list. The Servant's Flamestrike 24612 interrupt row is unchanged: kicks
-- are not DPS and keep the Fireball filler off the Servants the bots are waiting on.
--
-- Idempotent: the DELETE is scoped to the Servant's own boss entry (the shape tools/test_dungeon_plan_sql.py requires
-- of a later migration that INSERTs a (map, boss_entry) key again), so its interrupt row from 20261101020000 goes back
-- in unchanged beside the two hold_kill rows. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (15111);
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `comment`) VALUES
(309, 15111, 'interrupt', 0, 24612, 0, 0, 'Mad Servant: Flamestrike 24612 (castingTimeIndex 5, interruptFlags 15, preventionType 1): 1157-1343 fire in 5 yd of its target point plus 8 s burning ground'),
(309, 15111, 'hold_kill', 15146, 0, 0, 0, 'Mad Servant: no dps while a Mad Voidwalker lives (2 per dead Servant, 11 s later, at its corpse; Shadow Shock 24616 hits 15 yd)'),
(309, 15111, 'hold_kill', 15141, 0, 0, 0, 'Mad Servant: no dps while a Portal of Madness stands (its 2 Voidwalkers are on the way)');