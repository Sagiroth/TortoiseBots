-- Exact server-side character stats for the observability dashboard armory.
-- Written by ObservabilityEmitter for online bots (round-robin, ~100 s per
-- 500 bots) from the live Player: HP, attributes, armor, resistances, spell
-- power per school, healing, crit/dodge/parry/block, attack power, weapon
-- damage and speed, hit, MP5. Rage is stored in display units (0-100).
-- The core's own character_stats/character_armory_stats are written only on
-- logout, which bots never do, and lack spell crit/healing/MP5.

CREATE TABLE IF NOT EXISTS `tortoise_bots_armory_stats` (
    `guid` INT UNSIGNED NOT NULL,
    `maxhealth` INT UNSIGNED NOT NULL DEFAULT 0,
    `maxpower1` INT UNSIGNED NOT NULL DEFAULT 0,
    `maxpower2` INT UNSIGNED NOT NULL DEFAULT 0,
    `maxpower3` INT UNSIGNED NOT NULL DEFAULT 0,
    `maxpower4` INT UNSIGNED NOT NULL DEFAULT 0,
    `maxpower5` INT UNSIGNED NOT NULL DEFAULT 0,
    `strength` FLOAT NOT NULL DEFAULT 0,
    `agility` FLOAT NOT NULL DEFAULT 0,
    `stamina` FLOAT NOT NULL DEFAULT 0,
    `intellect` FLOAT NOT NULL DEFAULT 0,
    `spirit` FLOAT NOT NULL DEFAULT 0,
    `armor` INT NOT NULL DEFAULT 0,
    `resHoly` INT NOT NULL DEFAULT 0,
    `resFire` INT NOT NULL DEFAULT 0,
    `resNature` INT NOT NULL DEFAULT 0,
    `resFrost` INT NOT NULL DEFAULT 0,
    `resShadow` INT NOT NULL DEFAULT 0,
    `resArcane` INT NOT NULL DEFAULT 0,
    `spellDamage` INT NOT NULL DEFAULT 0,
    `spellDmgHoly` INT NOT NULL DEFAULT 0,
    `spellDmgFire` INT NOT NULL DEFAULT 0,
    `spellDmgNature` INT NOT NULL DEFAULT 0,
    `spellDmgFrost` INT NOT NULL DEFAULT 0,
    `spellDmgShadow` INT NOT NULL DEFAULT 0,
    `spellDmgArcane` INT NOT NULL DEFAULT 0,
    `healingPower` INT NOT NULL DEFAULT 0,
    `blockPct` FLOAT NOT NULL DEFAULT 0,
    `dodgePct` FLOAT NOT NULL DEFAULT 0,
    `parryPct` FLOAT NOT NULL DEFAULT 0,
    `meleeCritPct` FLOAT NOT NULL DEFAULT 0,
    `rangedCritPct` FLOAT NOT NULL DEFAULT 0,
    `spellCritPct` FLOAT NOT NULL DEFAULT 0,
    `attackPower` FLOAT NOT NULL DEFAULT 0,
    `rangedAttackPower` FLOAT NOT NULL DEFAULT 0,
    `meleeDmgMin` FLOAT NOT NULL DEFAULT 0,
    `meleeDmgMax` FLOAT NOT NULL DEFAULT 0,
    `rangedDmgMin` FLOAT NOT NULL DEFAULT 0,
    `rangedDmgMax` FLOAT NOT NULL DEFAULT 0,
    `meleeSpeed` FLOAT NOT NULL DEFAULT 0,
    `rangedSpeed` FLOAT NOT NULL DEFAULT 0,
    `meleeHit` FLOAT NOT NULL DEFAULT 0,
    `rangedHit` FLOAT NOT NULL DEFAULT 0,
    `spellHit` FLOAT NOT NULL DEFAULT 0,
    `manaRegen` INT NOT NULL DEFAULT 0,
    `updated` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
