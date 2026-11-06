-- Boss tactics F4-A (and F4-B data): the Gurubashi Bat Rider's blast also clears tanks out (map 309).
--
-- kind avoid_cast mode 'all' (docs/play/boss-tactics.md): while the row's spell is being cast, HoldsCaster is
-- false for every bot, so tanks walk out too. The Rider instakills itself, so there is no threat to keep. A
-- binary without F4-A turns the mode word into TacticMode::Invalid and skips the row, so this ships in the same
-- PR as the F4-A code. With the AiPlayerbot.DungeonRunAvoidCastAll key off, mode 'all' behaves exactly as the
-- default (a tank the caster is on holds it, row or not).
--
-- The F4-B half (`priority` = 50) is read only with its key on (AiPlayerbot.DungeonRunAvoidCastEarly): the
-- row's priority becomes the caster's own health % at or below which the blast zone is already live, before
-- any cast. It ships here so the zone data lands with the row.
--
-- Gurubashi Bat Rider 14750 (tortoise-wow src/scripts/dungeons/zulgurub/boss_jeklik.cpp npc_guru_bat_riderAI):
-- below 40 % health it turns fear-immune and casts Unstable Concoction 24024 on itself, once. spell_template
-- 24024: fire (school 2), castingTimeIndex 14 = 3000 ms, effect 1 school damage 4375-5625 (basePoints 4374, die
-- sides 1251) at TargetA 18 / TargetB 16 (enemies around the source at the caster) with radius index 13 = 10 yd
-- (SpellRadius.dbc), effect 2 instakill on the caster. `param` 13 is the 10 yd blast plus 3.
--
-- No interrupt row: a kick cannot stop it (preventionType 0 and interruptFlags 9 without the damage bit 0x02,
-- Spell::EffectInterruptCast), and creature_template 14750's mechanic_immune_mask holds stun, knockout and
-- silence, so no stun or silence breaks the cast either.
--
UPDATE `ai_boss_tactic` SET `tactic_mode` = 'all', `priority` = 50,
  `comment` = 'Gurubashi Bat Rider: Unstable Concoction 24024 with a 3 s self cast below 40 % hp (npc_guru_bat_rider) and 10 yd blast. all = tanks leave too. priority 50 = zone live from 50 % hp'
WHERE `map` = 309 AND `boss_entry` = 14750 AND `kind` = 'avoid_cast';
