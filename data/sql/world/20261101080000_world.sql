-- Boss tactics: Zul'Gurub's High Priest Venoxis (map 309) — the casters' stand seats, and Holy Fire on the
-- interrupt list.
--
-- Why: walk 22 (all-fixes-t1101g) met Venoxis four times and won once, the wipes leaving him at 6 %, 19 % and
-- ~43 %. One Holy Wrath 23979 chain killed nine bots inside one second in the r6 wipe (deaths at t+36.1 s with
-- 5376, 6989, 9086, 9450, 19962, 24944, 33449, 43857 and 57014) because the whole raid fought inside 16 yd of
-- him: 16 of 19 bots within 15 yd at t0, the healers 3.5-8.7 yd. Holy Wrath jumps to the unit nearest its last
-- one within 10 yd (Spell.cpp:2385-2450, CHAIN_SPELL_JUMP_RADIUS 10 yd plus bounding radii) and multiplies by 1.3
-- a jump, so the chain only has to reach the casters once to run through them. The seats hold the ranged and the
-- healers out of the melee clump and 11 yd apart, which leaves the chain inside the clump.
--
-- ai_boss_stand (the seat engine is the Onyxia C3 stand sets, ai/playerbot/BossStandPolicy.h; a row whose
-- window_spell is 0 applies for as long as the boss is at its anchor): one anchor at the boss's own P1 fight
-- spot, with a 7 yd radius. The radius is the boss's measured drift + 2 yd: while he fights at his spot the
-- three walk-22 fights put 14507 at -12015.8 -1690.5 (the kill), -12020.6 -1695.1 (r6) and -12019.1 -1696.7
-- (test2), all within 5.01 yd of the anchor below (frames.jsonl, hp < 100 %; every run whose boss holds his spot
-- stays within 7 yd, and the spawn -12029.8 -1707.9, 17.5 yd out, is only ever at 100 %).
--
-- The seats are checked against that whole disk, not the anchor point: the stand is up while the boss is anywhere
-- within the radius, so a seat has to stay at a good range wherever he is inside it. Nearest possible boss
-- distance = seat distance - 7 (Holy Nova 23858 reaches 15 yd, so >= 15), farthest = seat distance + 7 (a
-- caster's 30 yd range, so <= 30). That pins every seat to 22-23 yd from the anchor; the 12 seats sit at
-- 22.1-23.0 yd (worst-case boss distance 15.1-30.0 yd), the closest pair 11.58 yd apart (a Holy Wrath link needs
-- 10 yd, so 11 is enough), one bot a row (kBossStandSpacing 9 yd) and z inside the 27-43 band the boss is held
-- to. The room only holds 12 such seats: the map 309 navmesh has no band point at 118-142, 164-176, 185-193,
-- 217-235 or 279-298 degrees, and 11 yd of spacing at this radius needs about 28 degrees between neighbours, so
-- 12 is the most that fit. All 12 were checked on the map 309 navmesh from the plan's step-184 point
-- (-12008.266 -1687.4667 32.340786, ai_dungeon_step zul_gurub leg 1 seq 142) with
-- `turtle-agent path-check --mmaps ~/turtle-agent-mmaps --map 309`: every hop reads offline `complete` and
-- server `type 1 ok`, 0.5 yd short (the end-polygon tolerance). The 7 ranged and 5 healer seats cover the walk-22
-- roster's 8 ranged and 5 healer casters with one ranged bot spare-less: a caster the seats run out for keeps the
-- behaviour it had (it is not seated, so its class strategy places it and it fights on as before), the same
-- fallback the Onyxia rows use. The roles alternate around the ring so both are spread out.
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
-- Anchor 1: the boss's own fight spot, 7 yd radius. 12 seats, 22.1-23.0 yd out, every pair at least 11.58 yd apart.
(309, 14507, 1, -12018, -1695, 7, 'healer', 0, 0, 0, 0, -12038.09, -1705.89, 41.08, 0, 0, 1, 8000, 0, 'cast', 'Venoxis healer seat 0: 22.9 yd from the anchor, the boss 15.9-29.9 yd away anywhere in the disk, path-check ok'),
(309, 14507, 1, -12018, -1695, 7, 'ranged', 0, 0, 0, 0, -12029.88, -1714.64, 39.50, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 0: 23.0 yd from the anchor, the boss 16.0-30.0 yd away anywhere in the disk, path-check ok'),
(309, 14507, 1, -12018, -1695, 7, 'healer', 0, 0, 0, 1, -12018.23, -1717.85, 39.37, 0, 0, 1, 8000, 0, 'cast', 'Venoxis healer seat 1: 22.9 yd from the anchor, the boss 15.9-29.9 yd away anywhere in the disk, path-check ok'),
(309, 14507, 1, -12018, -1695, 7, 'ranged', 0, 0, 0, 1, -12006.51, -1714.87, 33.58, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 1: 23.0 yd from the anchor, the boss 16.0-30.0 yd away anywhere in the disk, path-check ok'),
(309, 14507, 1, -12018, -1695, 7, 'healer', 0, 0, 0, 2, -11998.22, -1706.64, 32.34, 0, 0, 1, 8000, 0, 'cast', 'Venoxis healer seat 2: 23.0 yd from the anchor, the boss 16.0-30.0 yd away anywhere in the disk, path-check ok'),
(309, 14507, 1, -12018, -1695, 7, 'ranged', 0, 0, 0, 2, -11995.05, -1695.40, 32.34, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 2: 23.0 yd from the anchor, the boss 16.0-30.0 yd away anywhere in the disk, path-check ok'),
(309, 14507, 1, -12018, -1695, 7, 'healer', 0, 0, 0, 3, -11997.89, -1684.15, 32.34, 0, 0, 1, 8000, 0, 'cast', 'Venoxis healer seat 3: 22.9 yd from the anchor, the boss 15.9-29.9 yd away anywhere in the disk, path-check ok'),
(309, 14507, 1, -12018, -1695, 7, 'ranged', 0, 0, 0, 3, -12005.74, -1675.60, 32.34, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 3: 22.9 yd from the anchor, the boss 15.9-29.9 yd away anywhere in the disk, path-check ok'),
(309, 14507, 1, -12018, -1695, 7, 'healer', 0, 0, 0, 4, -12016.77, -1672.08, 32.41, 0, 0, 1, 8000, 0, 'cast', 'Venoxis healer seat 4: 23.0 yd from the anchor, the boss 16.0-30.0 yd away anywhere in the disk, path-check ok'),
(309, 14507, 1, -12018, -1695, 7, 'ranged', 0, 0, 0, 4, -12027.94, -1675.31, 33.56, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 4: 22.1 yd from the anchor, the boss 15.1-29.1 yd away anywhere in the disk, path-check ok'),
(309, 14507, 1, -12018, -1695, 7, 'ranged', 0, 0, 0, 5, -12037.09, -1682.91, 39.34, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 5: 22.6 yd from the anchor, the boss 15.6-29.6 yd away anywhere in the disk, path-check ok'),
(309, 14507, 1, -12018, -1695, 7, 'ranged', 0, 0, 0, 6, -12040.94, -1694.25, 39.55, 0, 0, 1, 8000, 0, 'cast', 'Venoxis ranged seat 6: 23.0 yd from the anchor, the boss 16.0-30.0 yd away anywhere in the disk, path-check ok');
