-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834) — an opening threat hold and a longer
-- taunt window. The other Hakkar rows of 20261104020000_world.sql and the dispel rows of 20261103020000_world.sql
-- are unchanged.
--
-- D1 dps_hold_after, no spell, mode engage: a spell-less row fires its engage edge only, once per fight, so the hold
-- starts at the pull and runs `param` seconds (5). The loader accepts it (BossTacticTable::Add only needs 0 < param
-- <= 30 and a mode word its kind takes), and ReadDpsHold sets the engage trigger for exactly this shape. Gain: the
-- main tank holds the boss for the first seconds instead of the off-tank or a mage outthreatening it, so Cause
-- Insanity 24327 — cast on Hakkar's current victim every 20-25 s — is less likely to charm a damage dealer.
--
-- D2 taunt_after_cast, mode until_tank, `param` 20 (was 10): Cause Insanity charms its victim for 10 s and the core
-- restores that victim's threat 4 s after the aura ends, so the old 10 s deadline had expired before the boss could be
-- on a tank again and the row stayed spent.
--
-- Idempotent: each DELETE names one exact key (map, boss, kind, target, spell, health band), and this file re-inserts
-- both keys. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'dps_hold_after' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'taunt_after_cast' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'dps_hold_after', 0, 0, 0, 5, 'engage', 0, 0, 0, 0, 0, 0, 'Hakkar: hold DPS 5 s from the first engagement so the main tank holds him before Cause Insanity 24327 picks a damage dealer as its victim'),
(309, 14834, 'taunt_after_cast', 0, 0, 1, 20, 'until_tank', 0, 0, 0, 0, 0, 0, 'Hakkar: Cause Insanity 24327 charms his victim 10 s and the core restores its threat 4 s later; tanks[1] takes him back within 20 s while he is on a non-tank');
