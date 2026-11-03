-- Boss tactics: pace the Mad Servant 15111 kills in Zul'Gurub (map 309) to one Servant at a time, and hold area
-- damage and attacker DoTs on a held Servant.
--
-- Walk 22 (both hold_kill rows of 20261101050000 live): the rows could not stop the first burst. At the pull no Mad
-- Voidwalker or Portal of Madness exists yet, so KillHeld is false for everyone: at most 3-4 Servants of one wave are
-- engaged (D's 4, A's 3 about 80-100 s later), the free DPS split over 2-3 of them (dps target tournament, the group
-- tank's victim, class AoE) and the first Servant died 11-17 s in with the others already at 18-75 % health. From
-- there each remaining Servant lost another 10-30 % per frame, because the hold only ever zeroed the dps target
-- choosers: class AoE and thrown grenades (40 Thorium Grenades, 15 Rain of Fire, 8 Multi-Shot in one fight) ran on the
-- Voidwalkers that spawn at the Servant corpses, inside the pile; Corruption / Curse of Agony / Siphon Life / Serpent
-- Sting from the "attacker without aura" rows ticked on after the hold; and every focus-ordered bot was exempt from
-- both hold clauses (an explicit attack order wins).
--
-- one_at_a_time (docs/play/boss-tactics.md): all DPS kill one living unit of the entry at a time. The pick is the
-- lowest-health unit of the entry that fights, the lowest guid on a tie, computed by every bot from the same cached
-- attacker list, so no bot needs a vote: the first Servant engaged becomes the pick and the whole raid converges on
-- it. Every other Servant of the entry waits, and the code also vetoes area damage and attacker debuffs on any bot
-- that is not a tank or a healer whenever a held (or non-pick) Servant stands within 10 yd of the cast's target or of
-- the bot. Cost: about 7 s per Servant with all DPS on it, then about 25 s for the portal and its pair, so the D+A
-- fight fits in the roughly 230 s walk 21 planned.
--
-- Idempotent: the DELETE is scoped to the Servant's own boss entry (the shape tools/test_dungeon_plan_sql.py requires
-- of a later migration that INSERTs a (map, boss_entry) key again), so its interrupt row and the two hold_kill rows
-- from 20261101050000 go back in unchanged beside the new one_at_a_time row. Never edit this file once applied; add a
-- newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (15111);
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `comment`) VALUES
(309, 15111, 'interrupt', 0, 24612, 0, 0, 'Mad Servant: Flamestrike 24612 (castingTimeIndex 5, interruptFlags 15, preventionType 1): 1157-1343 fire in 5 yd of its target point plus 8 s burning ground'),
(309, 15111, 'hold_kill', 15146, 0, 0, 0, 'Mad Servant: no dps while a Mad Voidwalker lives (2 per dead Servant, 11 s later, at its corpse; Shadow Shock 24616 hits 15 yd)'),
(309, 15111, 'hold_kill', 15141, 0, 0, 0, 'Mad Servant: no dps while a Portal of Madness stands (its 2 Voidwalkers are on the way)'),
(309, 15111, 'one_at_a_time', 0, 0, 0, 0, 'Mad Servant: all dps on one Servant at a time (two die together = four Voidwalkers together)');
