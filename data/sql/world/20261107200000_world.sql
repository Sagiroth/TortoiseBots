-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834), delete the dead taunt_after_cast row.
--
--   taunt_after_cast  deleted: Hakkar is immune to taunts (creature_template 14834 immunity_flags 6 =
--                     TAUNT|MOD_STAT), so the Cause Insanity 24327 row (20261104020000) can never take him back;
--                     it only burns the off-tank's taunt. Deleted by its exact key; the older file stays as shipped.
--                     The charm reset is already answered by the threat_successor cap (20261105210000), which keeps
--                     the damage dealers below the off-tank so it inherits him.
-- Every other Hakkar row stays: the dispel_priority rows, seek_aura, pull_gate, spread, threat_successor, anchor
-- and the opening dps_hold_after row.
--
-- Idempotent: the DELETE names one exact (map, boss, kind, target, spell, health band) key and inserts nothing.
-- Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'taunt_after_cast' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
