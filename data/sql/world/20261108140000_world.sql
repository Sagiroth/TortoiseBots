-- Combat timeline aura watch (AiPlayerbot.CombatTimeline): the two Jin'do auras the Zul'Gurub fights were missing, so a
-- finished fight record can say how long a Hex or a Brain Wash lasted.
--
-- Read once at startup into a cache (runtime/CombatMonitorService.cpp LoadTimelineWatch, one pass over the rows).
-- Both ids were checked against the core's own data and the Zul'Gurub boss script
-- (src/scripts/dungeons/zulgurub/boss_jindo.cpp):
--   17172 Hex         Jin'do (SPELL_HEX, boss_jindo.cpp:33): magic, 5 s, the dispel_priority row cures it
--   24261 Brain Wash  Jin'do's Brain Wash Totem 15112 (SPELL_BRAINWASH, boss_jindo.cpp:38): a charm aura
--
-- Idempotent: the DELETE removes only these two spell ids, so rows added for other auras survive a re-run. Never edit
-- this file once applied; add a newer migration.

DELETE FROM `ai_combat_aura_watch` WHERE `spell_id` IN (17172, 24261);

INSERT INTO `ai_combat_aura_watch` (`spell_id`, `comment`) VALUES
(17172, 'Zul''Gurub: Hex (Jin''do, SPELL_HEX boss_jindo.cpp:33)'),
(24261, 'Zul''Gurub: Brain Wash (Jin''do totem 15112, SPELL_BRAINWASH boss_jindo.cpp:38)');
