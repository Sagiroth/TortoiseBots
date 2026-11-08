-- Combat timeline aura watch, second batch (AiPlayerbot.CombatTimeline): Jin'do's Hex and Brainwash, so the Fight debugger's Auras
-- panel can show how long Hex stayed on a tank and who was brainwashed. The ids live here, never in code.
--
-- Read once at startup into a cache (runtime/CombatMonitorService.cpp LoadTimelineWatch, one pass over the rows).
-- Both ids come from the Zul'Gurub boss script (src/scripts/dungeons/zulgurub/boss_jindo.cpp):
--   17172 Hex           Jin'do (SPELL_HEX, cast on his victim)
--   24261 Brainwash     Jin'do's Brain Wash Totem (SPELL_BRAINWASH; the core spell is named "Brain Wash")
--
-- Idempotent: the DELETE removes only these two spell ids, so rows added for other auras survive a re-run. Never edit
-- this file once applied; add a newer migration.

CREATE TABLE IF NOT EXISTS `ai_combat_aura_watch` (
  `spell_id` int unsigned NOT NULL,
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

DELETE FROM `ai_combat_aura_watch` WHERE `spell_id` IN (17172, 24261);

INSERT INTO `ai_combat_aura_watch` (`spell_id`, `comment`) VALUES
(17172, 'Zul''Gurub: Hex (Jin''do)'),
(24261, 'Zul''Gurub: Brainwash (Jin''do totem)');
