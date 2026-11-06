-- Jin'do the Hexxer (11380): the Powerful Healing Ward 14987 outranks the Brain Wash Totem 15112. The ward's aura
-- 24310 heals him about as much as the whole raid deals (about 3,300 hp/s), so a fight in which it lives never goes
-- down; the totem only charms one target per cast. A better rank takes a held focus at once (DungeonRunPolicy.h,
-- PickKillFirst), so with the ward first every new ward is focused even while a half-dead totem still holds the focus.
--
-- Idempotent: both rows are deleted by their exact key and inserted again with the swapped priorities.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'kill_first' AND `target_entry` = 15112 AND `spell_id` = 0;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 11380 AND `kind` = 'kill_first' AND `target_entry` = 14987 AND `spell_id` = 0;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `bot_aura`, `tactic_mode`, `comment`) VALUES
(309, 11380, 'kill_first', 14987, 0, 1, 0, 0, '', 'Jindo: Powerful Healing Ward 14987 first (its aura 24310 heals him about as much as the raid deals)'),
(309, 11380, 'kill_first', 15112, 0, 2, 0, 0, '', 'Jindo: Brain Wash Totem 15112 after the ward (it charms a random attacker; Avoidance makes it immune to area damage)');
