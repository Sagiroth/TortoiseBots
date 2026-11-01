-- Boss tactics (turtle-agent#246): drop six raid interrupt rows the core cannot interrupt.
--
-- Checked against tortoise-wow with TORTOISE_CORE set (tools/test_boss_tactics_sql.py,
-- BossTacticsCoreData.test_interrupt_spells_are_interruptible), which fails on these
-- rows today:
--   map 309 boss 14507 spell 23895 (ZG Venoxis Renew): castingTimeIndex 1, the cast is instant.
--   map 309 boss 14834 spell 24324 (ZG Hakkar Bloodsiphon Stun): preventionType 0, not SILENCE (1).
--   map 409 boss 12118 spell 20604 (MC Lucifron's Flamewaker Protector Dominate Mind, target 12119):
--     interruptFlags 0xd lacks SPELL_INTERRUPT_FLAG_DAMAGE 0x2 (its own comment says flags 13).
--   map 531 boss 15275 spell 7393 and boss 15276 spell 7393 (AQ40 Twin Emperors Heal Brother):
--     preventionType 0, not SILENCE (1).
--   map 532 boss 61224 spell 57094 (Karazhan halls Grizikil Grellkin Fireball):
--     preventionType 0, not SILENCE (1).
-- The bots only wasted a kick on these casts, so removing the rows is a fix. A replacement
-- interrupt row needs a different, interruptible spell per boss from the core's boss script;
-- that design work belongs to the raid preps, not here.
--
-- Idempotent: each DELETE removes one row key. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14507 AND `kind` = 'interrupt' AND `target_entry` = 0 AND `spell_id` = 23895;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'interrupt' AND `target_entry` = 0 AND `spell_id` = 24324;
DELETE FROM `ai_boss_tactic` WHERE `map` = 409 AND `boss_entry` = 12118 AND `kind` = 'interrupt' AND `target_entry` = 12119 AND `spell_id` = 20604;
DELETE FROM `ai_boss_tactic` WHERE `map` = 531 AND `boss_entry` = 15275 AND `kind` = 'interrupt' AND `target_entry` = 0 AND `spell_id` = 7393;
DELETE FROM `ai_boss_tactic` WHERE `map` = 531 AND `boss_entry` = 15276 AND `kind` = 'interrupt' AND `target_entry` = 0 AND `spell_id` = 7393;
DELETE FROM `ai_boss_tactic` WHERE `map` = 532 AND `boss_entry` = 61224 AND `kind` = 'interrupt' AND `target_entry` = 0 AND `spell_id` = 57094;
