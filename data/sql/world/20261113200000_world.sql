-- Onyxia's Lair (map 249), walk 2i: the slot-0 tank_post holds two healers on its tank and answers her Bellowing Roar.
--
-- Walk 2g and 2h evidence: the slot-0 post runs, and the main tank still died at +50 s in 15 of 15 P3 pulls. Two of
-- the 7-9.5 healers alive at the landing heal one target and nothing else with AiPlayerbot.DungeonRunTankPostHealers,
-- and stay within 30 yd of the holder; the row's `spell_id` names Bellowing Roar 18431, whose aura on a raid member
-- marks the cast the holder answers with its class's defensive actions (warrior shield wall then last stand; druid
-- frenzied regeneration then barkskin) inside a 10 s window opened by the first sighting after she was on the tank,
-- once per post. The other three post rows keep `spell_id` 0 and `healers` 0; no stand row changes.
--
-- The column: `healers` is a tinyint (0-4) on `hold_spot` tank_post rows only, added through
-- information_schema.COLUMNS + PREPARE (MySQL 8 and MariaDB), and only when it is missing.
--
-- Idempotent: the column is added only when it is absent, and the UPDATE names exactly the (249, 10184) slot-0 post
-- row on 10184 in the 0-40 band. Never edit this file once applied; add a newer migration.

SET @s := (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE `ai_boss_tactic` ADD COLUMN `healers` tinyint unsigned NOT NULL DEFAULT 0 AFTER `spot_z`',
  'DO 0')
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ai_boss_tactic' AND COLUMN_NAME = 'healers');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

UPDATE `ai_boss_tactic` SET `spell_id` = 18431, `healers` = 2, `comment` = 'Onyxia P3: slot 0 holds her at the east spot E; 2 healers stay within 30 yd and heal only this tank, and it uses its defensive cooldown at the first Bellowing Roar 18431 (walks 2g-2h: the main tank dead at +50 s in 15 of 15 P3 pulls; needs AiPlayerbot.DungeonRunTankPostMain and AiPlayerbot.DungeonRunTankPostHealers)' WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'tank_post' AND `target_entry` = 10184 AND `hp_max` = 40;
