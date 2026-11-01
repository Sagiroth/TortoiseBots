-- Boss tactics: Zul'Gurub's High Priest Venoxis (map 309) — the casters' stand seats, and Holy Fire on the
-- interrupt list.
--
-- Why: walk 22 (all-fixes-t1101g) met Venoxis four times and won once, the wipes leaving him at 6 %, 19 % and
-- ~43 %. One Holy Wrath 23979 chain killed nine bots inside one second in the r6 wipe (deaths at t+36.1 s with
-- 5376, 6989, 9086, 9450, 19962, 24944, 33449, 43857 and 57014) because the whole raid fought inside 16 yd of
-- him: 16 of 19 bots within 15 yd at t0, the healers 3.5-8.7 yd. Holy Wrath jumps to the unit nearest its last
-- one within 10 yd (Spell.cpp:2385-2450, CHAIN_SPELL_JUMP_RADIUS 10 yd plus bounding radii) and multiplies by 1.3
-- a jump, so the chain only has to reach the casters once to run through them. The seats hold the ranged and the
-- healers out of the melee clump and 12 yd apart, which leaves the chain inside the clump (8-11 units, k8-k11 =
-- 2.6-4.1 k a hit).
--
-- ai_boss_stand (the seat engine is the Onyxia C3 stand sets, ai/playerbot/BossStandPolicy.h; a row whose
-- window_spell is 0 applies for as long as the boss is at its anchor): one anchor at the boss's own P1 fight
-- spot. The frames of the walk-22 fights put 14507's stationary P1 position at -12015.8 -1690.5 (the kill),
-- -12020.9 -1696.2 (r6) and -12019.1 -1696.7 (test2), all within 5 yd of the anchor below, so anchor_r 15 covers
-- every one of them and not the spawn (-12029.8 -1707.9, 17.5 yd out, which only matters before the pull).
--
-- The seats: 17-26 yd from the anchor (outside Holy Nova 23858's 15 yd, inside a caster's 30 yd range), each at
-- least 12 yd from every other (a chain link needs 10), one bot a row because kBossStandSpacing 9 yd is under
-- that jump, and z inside the 27-43 band the boss itself is held to. Every seat is at least 17 yd from all three
-- measured boss positions, so the boss's own 5 yd of drift toward the raid cannot put a caster back inside the
-- chain's reach. All 13 were checked on the map 309 navmesh from the plan's step-184 point
-- (-12008.266 -1687.4667 32.340786, ai_dungeon_step zul_gurub leg 1 seq 142) with
-- `turtle-agent path-check --mmaps ~/turtle-agent-mmaps --map 309`: every hop reads offline `complete` and
-- server `type 1 ok`, 0.5 yd short (the end-polygon tolerance). The 8 ranged and 5 healer seats cover the 11
-- casters of the walk-22 roster with slack, and the roles alternate around the ring so both are spread out.
--
-- ai_boss_tactic: the Renew 23895 interrupt row never worked — Renew is instant (castingTimeIndex 1) and
-- Spell::EffectInterruptCast (SpellEffects.cpp:3389) needs a cast time and SPELL_INTERRUPT_FLAG_DAMAGE — and the
-- core check of tools/test_boss_tactics_sql.py rejects it. Holy Fire 23860 (boss_venoxis.cpp:216-222, on the
-- victim every 8-12 s in phase 1) is castingTimeIndex 22, interruptFlags 15 and preventionType 1, so a class kick
-- stops it (Leor's Pummel already interrupted it at t+10.0 s in the walk-22 kill). No kill_first row for the
-- Razzashi Cobra 11373: the plan already pulls and kills them first.
--
-- Idempotent: both DELETEs remove only these rows' keys. Never edit this file once applied; add a newer
-- migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14507);
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `comment`) VALUES
(309, 14507, 'interrupt', 0, 23860, 0, 0, 'Venoxis: Holy Fire 23860 (castingTimeIndex 22, interruptFlags 15, preventionType 1; boss_venoxis.cpp:216-222)');

DELETE FROM `ai_boss_stand` WHERE `map` = 309 AND `boss_entry` = 14507;
INSERT INTO `ai_boss_stand` (`map`, `boss_entry`, `anchor`, `anchor_x`, `anchor_y`, `anchor_r`, `role`, `window_spell`, `aura`, `side`, `seq`, `x`, `y`, `z`, `dx`, `dy`, `slots`, `hold_ms`, `release_anchor`, `window_signal`, `comment`) VALUES
-- Anchor 1: the boss's own fight spot. 13 seats, 17.4-26.0 yd out, every pair at least 12.27 yd apart.
(309, 14507, 1, -12018, -1695, 15, 'ranged', 0, 0, 0, 0, -12038.00, -1697.00, 39.55, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 0: 20.1 yd from the anchor, 17.1 yd from the boss, path-check ok'),
(309, 14507, 1, -12018, -1695, 15, 'ranged', 0, 0, 0, 1, -12000.25, -1677.00, 32.34, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 1: 25.3 yd from the anchor, 20.6 yd from the boss, path-check ok'),
(309, 14507, 1, -12018, -1695, 15, 'ranged', 0, 0, 0, 2, -11992.50, -1699.75, 32.34, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 2: 25.9 yd from the anchor, 25.1 yd from the boss, path-check ok'),
(309, 14507, 1, -12018, -1695, 15, 'ranged', 0, 0, 0, 3, -12039.00, -1709.25, 42.27, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 3: 25.4 yd from the anchor, 22.3 yd from the boss, path-check ok'),
(309, 14507, 1, -12018, -1695, 15, 'ranged', 0, 0, 0, 4, -12041.27, -1684.95, 40.55, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 4: 25.4 yd from the anchor, 23.3 yd from the boss, path-check ok'),
(309, 14507, 1, -12018, -1695, 15, 'ranged', 0, 0, 0, 5, -12022.75, -1669.50, 32.76, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 5: 25.9 yd from the anchor, 22.1 yd from the boss, path-check ok'),
(309, 14507, 1, -12018, -1695, 15, 'ranged', 0, 0, 0, 6, -12030.00, -1717.75, 39.77, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 6: 25.7 yd from the anchor, 23.4 yd from the boss, path-check ok'),
(309, 14507, 1, -12018, -1695, 15, 'ranged', 0, 0, 0, 7, -12004.93, -1717.41, 33.00, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 7: 26.0 yd from the anchor, 25.1 yd from the boss, path-check ok'),
(309, 14507, 1, -12018, -1695, 15, 'healer', 0, 0, 0, 0, -11993.50, -1687.50, 32.34, 0, 0, 1, 8000, 0, 'cast', 'Venoxis healer seat 0: 25.6 yd from the anchor, 22.5 yd from the boss, path-check ok'),
(309, 14507, 1, -12018, -1695, 15, 'healer', 0, 0, 0, 1, -12010.50, -1670.25, 32.34, 0, 0, 1, 8000, 0, 'cast', 'Venoxis healer seat 1: 25.9 yd from the anchor, 20.9 yd from the boss, path-check ok'),
(309, 14507, 1, -12018, -1695, 15, 'healer', 0, 0, 0, 2, -12034.00, -1675.00, 39.85, 0, 0, 1, 8000, 0, 'cast', 'Venoxis healer seat 2: 25.6 yd from the anchor, 23.9 yd from the boss, path-check ok'),
(309, 14507, 1, -12018, -1695, 15, 'healer', 0, 0, 0, 3, -12018.00, -1720.75, 39.37, 0, 0, 1, 8000, 0, 'cast', 'Venoxis healer seat 3: 25.8 yd from the anchor, 24.1 yd from the boss, path-check ok'),
(309, 14507, 1, -12018, -1695, 15, 'healer', 0, 0, 0, 4, -12003.73, -1705.00, 32.34, 0, 0, 1, 8000, 0, 'cast', 'Venoxis healer seat 4: 17.4 yd from the anchor, 17.5 yd from the boss, path-check ok');
