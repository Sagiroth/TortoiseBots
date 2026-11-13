-- Boss tactics: Hakkar the Soulflayer (Zul'Gurub, map 309, creature 14834) — the healers' top-up reserve. Walk 70B
-- and walk 71: healer mana ran 94 / 73 / 56 / 42 / 32 / 23 % at 0 / 30 / 60 / 90 / 120 / 180 s, and the first 30 s
-- (raid health at 98 %) cost 21 points, most of it the almost-full top-up tier (Flash of Light, rejuvenation spread,
-- renew). A Corrupted Blood cast lands on 3.5-4.0 members 10-30 s after a siphon and on 8.2-11.2 members 30-60 s
-- after it, so the top-ups are cheap before the gather and expensive during it.
--
-- 85 %: the healer may spend about 9 points on top-ups, then stops until the release window; the aim is to keep about
-- 12 of the first 30 s' 21 points. 60 s: the gather runs 32-68 s after a siphon, so the reserve is released for
-- exactly the part where a landing lands on 8-11 members, and kept in the opener and in the 22 s after each siphon.
-- Tanks and Hakkar's current victim are exempt; the medium, low and critical heal tiers never change. Behind
-- AiPlayerbot.DungeonRunHealReserve (default 0). When the r11-1 park ships, its release lead plus 5 s replaces 60 by
-- a newer migration.
--
-- Idempotent: the DELETE names one exact (map, boss, kind, target, spell, health band) key, and this file's own row is
-- the only one it writes. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14834 AND `kind` = 'heal_reserve' AND `target_entry` = 0 AND `spell_id` = 0 AND `hp_min` = 0 AND `hp_max` = 100;

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `tactic_mode`, `window_s`, `cycle_spell`, `cycle_s`, `spot_x`, `spot_y`, `spot_z`, `comment`) VALUES
(309, 14834, 'heal_reserve', 0, 0, 0, 85, '', 60, 0, 0, 0, 0, 0, 'Hakkar: healers keep 85 % mana from top-ups outside the 60 s before each Blood Siphon; the gather and its Corrupted Blood landings get the reserve');
