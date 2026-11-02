-- Boss tactics: Zul'Gurub's High Priest Thekal (map 309) is a die-together group, not a kill order.
--
-- Thekal 14599 fights beside the Zealots Lor'Khan 11347 and Zath 11348. When one of the three dies while another lives,
-- the living one raises it to full health 7 s later (boss_thekal.cpp: RESSURECT_TIMER 7000, DoRessurectUnit), with a
-- second chance 8 s after that if the chosen raiser died first. Only all three corpses inside that window kill the
-- phase: each keeps the others alive, so killing them one at a time (what the raid does with the tank's victim
-- followed by the whole raid, DpsTargetValue's tank-victim priority) restores the kill and the encounter never ends.
-- Walk 27 C measured 44 restorations in 34 min in one run and 32 in 20 min in another; the trio's pool is 325 540 hp
-- and the raid's damage 2.2-3.0 k dps, so a hold at 10 % leaves 33 k hp (11-15 s, outside the timer) and a hold at 3 %
-- leaves about 10 k hp (3-5 s, inside it).
--
-- linked_death (docs/play/boss-tactics.md): the rows of one (boss_entry, priority) key are one group, the boss_entry is
-- the anchor and each target_entry a partner. While more than one member lives, free DPS bots take the member with the
-- highest health; no member is damaged at or below param % while another is above it; once every living member is at or
-- below param % the hold ends and the ordinary pick burns the lowest, so the three die together inside the 7 s timer.
-- priority 1 is the group id, so map 309 can carry another linked group later; param 3.0 is the hold percent.
--
-- Idempotent: the DELETE is scoped to the anchor's own boss entry, so re-running replaces these two rows and leaves any
-- other Thekal tactic row alone. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14599);
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `comment`) VALUES
(309, 14599, 'linked_death', 11347, 0, 1, 3.0, 'Thekal: Zealot Lor''Khan is raised to full health 7 s after a death while a sibling lives; all three must die inside that window, so the hold is 3 %'),
(309, 14599, 'linked_death', 11348, 0, 1, 3.0, 'Thekal: Zealot Zath is raised to full health 7 s after a death while a sibling lives; all three must die inside that window, so the hold is 3 %');
