-- Gear weight scale review across all classes.
--
-- Weights are per point of a stat, per 1% of hit/crit/dodge/parry/block, and
-- per 1 weapon DPS, so each scale can be checked against Vanilla conversions
-- (1 Str = 2 AP for most melee, 1 weapon DPS = 14 AP). Fixes:
--
-- Enhancement (21): atkpwr 35 against str 4 made +AP items worth ~17x their
--   Strength equivalent and more than a weapon upgrade. atkpwr 2, like the
--   other melee scales.
-- Discipline (13) heals but had no splheal, so pure +healing items scored 0.
--   splheal 29, as holy (14).
-- Balance (29) casts Wrath and Insect Swarm (Nature) but only weighted Arcane.
--   natsplpwr 11, equal to arcsplpwr.
-- Rogues (10-12): mledps 10 valued 1 weapon DPS at 5 AP. mledps 28 (14 AP).
--
-- Stamina from the earlier healer/warrior stamina migration was one absolute
-- value for scales of very different size. Against small primary weights it
-- outranked the main stats:
--   resto druid (31): int 3 / spi 5 / sta 10 against splheal 10 -> int 10,
--     spi 8, sta 4;
--   holy paladin (4): sta 12 against splheal 5 -> sta 2;
--   resto shaman (22): sta 10 against splheal 17 -> sta 5;
--   Priests keep theirs (10-12 against int/spi/splheal ~30).
--
-- Arms/fury (1, 2) are damage dealers: sta 15 against str 4 sent them into
--   tank gear. In AP terms (atkpwr 2 = 1 AP) 1% crit and 1% hit are worth
--   ~28 AP each for a warrior and 20 Agility give 1% crit, so crit and hit
--   56 (were 40), agi 3 (was 2), sta 1 (tie-break only, as the other DPS
--   scales). str 4, atkpwr 2 and mledps 28 (1 DPS = 14 AP) already fit.
--
-- Tanks: 1% dodge/parry/block weighted about as much as 1 defense skill,
--   while ~25 defense skill gives ~1% of each. Protection warrior (3) and
--   paladin (5): dodge 45, parry 40, block chance 30. Feral tank (30) had no
--   threat stats: str 3, feratkpwr 2.
-- Casters: spell penetration 27-30 per point priced 1 penetration at 3-4
--   spell power; spellpenrtng 5. Intellect 1 on mages, elemental and balance
--   is raised to 3.
--
-- Idempotent: the touched (id, field) rows are deleted before re-inserting
-- (the table has no unique key). The character-side companion migration
-- empties the equipment cache so seed gear is rebuilt with these weights.

DELETE FROM `ai_playerbot_weightscale_data`
WHERE (`id` = 21 AND `field` = 'atkpwr')
   OR (`id` = 13 AND `field` = 'splheal')
   OR (`id` = 29 AND `field` = 'natsplpwr')
   OR (`id` IN (10,11,12) AND `field` = 'mledps')
   OR (`id` = 31 AND `field` IN ('int','spi','sta'))
   OR (`id` IN (1,2) AND `field` IN ('sta','critstrkrtng','hitrtng','agi'))
   OR (`id` IN (4,22) AND `field` = 'sta')
   OR (`id` IN (3,5) AND `field` IN ('dodgertng','parryrtng','blockrtng'))
   OR (`id` = 30 AND `field` IN ('str','feratkpwr'))
   OR (`id` IN (20,23,24,25) AND `field` = 'spellpenrtng')
   OR (`id` IN (20,23,24,25,29) AND `field` = 'int');

INSERT INTO `ai_playerbot_weightscale_data` (`id`, `field`, `val`) VALUES
	(21, 'atkpwr', 2),
	(13, 'splheal', 29),
	(29, 'natsplpwr', 11),
	(10, 'mledps', 28),
	(11, 'mledps', 28),
	(12, 'mledps', 28),
	(31, 'int', 10),
	(31, 'spi', 8),
	(31, 'sta', 4),
	(1, 'sta', 1),
	(2, 'sta', 1),
	(1, 'critstrkrtng', 56),
	(2, 'critstrkrtng', 56),
	(1, 'hitrtng', 56),
	(2, 'hitrtng', 56),
	(1, 'agi', 3),
	(2, 'agi', 3),
	(4, 'sta', 2),
	(22, 'sta', 5),
	(3, 'dodgertng', 45),
	(3, 'parryrtng', 40),
	(3, 'blockrtng', 30),
	(5, 'dodgertng', 45),
	(5, 'parryrtng', 40),
	(5, 'blockrtng', 30),
	(30, 'str', 3),
	(30, 'feratkpwr', 2),
	(20, 'spellpenrtng', 5),
	(23, 'spellpenrtng', 5),
	(24, 'spellpenrtng', 5),
	(25, 'spellpenrtng', 5),
	(20, 'int', 3),
	(23, 'int', 3),
	(24, 'int', 3),
	(25, 'int', 3),
	(29, 'int', 3);
