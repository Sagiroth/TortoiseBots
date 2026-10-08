-- Remove hunter pet training spells accidentally learned by non-hunter bots.
DELETE FROM `character_spell`
WHERE `spell` IN (6666, 6667)
  AND `guid` NOT IN (SELECT `guid` FROM `characters` WHERE `class` = 3);
