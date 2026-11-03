-- Boss tactics: the avoid_cast row for the Gurubashi Bat Rider's explosion in Zul'Gurub (map 309).
--
-- kind avoid_cast (docs/play/boss-tactics.md): while a creature with `boss_entry` (or `target_entry` when set) is in
-- the fight and casts `spell_id`, every bot that is not a tank and stands within `param` yd of it walks out of that
-- radius and stays out until the cast ends or the caster dies. The row activates on any creature entry, trash
-- included: the strategy reads the entries among the bots' attackers.
--
-- Gurubashi Bat Rider 14750 (tortoise-wow src/scripts/dungeons/zulgurub/boss_jeklik.cpp npc_guru_bat_riderAI): below
-- 40 % health it turns fear-immune and casts Unstable Concoction 24024 on itself, once (GoingToExplose stays set, so
-- a cast that does not finish is never retried). spell_template 24024 (tw_world_spell_template.sql): fire (school 2),
-- castingTimeIndex 14 = 3000 ms (SpellCastTimes.dbc), effect 1 school damage 4375-5625 (basePoints 4374, dieSides
-- 1251) at TargetA 18 / TargetB 16 (enemies around the source at the caster) with radius index 13 = 10 yd
-- (SpellRadius.dbc of the m910q server's data/dbc), effect 2 instakill on the caster. The core hits a player whose
-- centre is within the radius in 3D, with no size factor (Spell.cpp SpellNotifierCreatureAndPlayer PUSH_SRC_CENTER),
-- so `param` is the 10 yd plus 3.
--
-- No interrupt row: a kick cannot stop it (preventionType 0 and interruptFlags 9 without the damage bit 0x02,
-- Spell::EffectInterruptCast), and creature_template 14750's mechanic_immune_mask 1019952979 holds stun (12),
-- knockout (14) and silence (9), so no stun or silence breaks the cast either.
--
-- Idempotent: the DELETE removes only this row's key, then it goes back in. Never edit this file once applied; add a
-- newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` = 14750 AND `kind` = 'avoid_cast' AND `target_entry` = 0 AND `spell_id` = 24024;
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `comment`) VALUES
(309, 14750, 'avoid_cast', 0, 24024, 0, 13, 'Gurubashi Bat Rider: Unstable Concoction 24024, 3 s self cast, 10 yd fire blast (boss_jeklik.cpp)');
