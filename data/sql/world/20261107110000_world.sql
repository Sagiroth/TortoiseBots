-- Combat timeline aura watch (AiPlayerbot.CombatTimeline): the auras a finished fight record lists when one of our bots
-- gains or loses them. The ids live here, never in code; a new boss or mechanic adds rows.
--
-- Read once at startup into a cache (runtime/CombatMonitorService.cpp LoadTimelineWatch, one pass over the rows).
-- Every id was checked against the core's own data (~/tortoise-wow sql/base/tw_world_spell_template.sql) and the
-- Zul'Gurub boss scripts (src/scripts/dungeons/zulgurub/boss_arlokk.cpp, boss_jindo.cpp, boss_hakkar.cpp):
--   24210 Mark of Arlokk        Arlokk (SPELL_MARK)
--   24306 Delusions of Jin'do   Jin'do (SPELL_DELUSIONS_OF_JINDO)
--   24466 Banish                Jin'do (SPELL_BANISH)
--   24321 Poisonous Blood       Hakkar (SPELL_POISONOUS_BLOOD)
--   24327 Cause Insanity        Hakkar (SPELL_CAUSEINSANITY; the charm)
--   24328 Corrupted Blood       Hakkar (SPELL_CORRUPTEDBLOOD)
--
-- Idempotent: the DELETE removes only these six spell ids, so rows added for other auras survive a re-run. Never edit
-- this file once applied; add a newer migration.

CREATE TABLE IF NOT EXISTS `ai_combat_aura_watch` (
  `spell_id` int unsigned NOT NULL,
  `comment` varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

DELETE FROM `ai_combat_aura_watch` WHERE `spell_id` IN (24210, 24306, 24321, 24327, 24328, 24466);

INSERT INTO `ai_combat_aura_watch` (`spell_id`, `comment`) VALUES
(24210, 'Zul''Gurub: Mark of Arlokk (Arlokk)'),
(24306, 'Zul''Gurub: Delusions of Jin''do (Jin''do)'),
(24321, 'Zul''Gurub: Poisonous Blood (Hakkar)'),
(24327, 'Zul''Gurub: Cause Insanity (Hakkar)'),
(24328, 'Zul''Gurub: Corrupted Blood (Hakkar)'),
(24466, 'Zul''Gurub: Banish (Jin''do)');
