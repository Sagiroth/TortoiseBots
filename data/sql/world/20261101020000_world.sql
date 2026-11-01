-- Boss tactics: Zul'Gurub (map 309, High Priest Venoxis leg) rows for the Mad Servant 15111 and the Mad Voidwalker
-- 15146 it leaves behind.
--
-- Mad Servant 15111 (spell list 151110): Fireball 24611 (castingTimeIndex 3) and Flamestrike 24612
-- (castingTimeIndex 5, interruptFlags 15, preventionType 1): 1157-1343 fire to every enemy within radius index 8 =
-- 5 yd of its target point plus an 8 s burning ground. On death (creature_ai_events 1511103, event 6) it casts
-- Portal of Madness 24621 at its own spot, and that portal's event (1514101, 1 s then every 30 s) casts 24620, an
-- aura whose 24622 summons 2 Mad Voidwalkers there, inside the party's pile.
--
-- Mad Voidwalker 15146: Consuming Shadows 24614 (creature_ai_events 1514601, while its target is within 5 yd, every
-- 4-8 s) is a 15 s channel (attributesEx 0x40) that triggers 24615 every second, 400 shadow to every enemy within
-- radius index 8 = 5 yd of the voidwalker. Several voidwalkers channelling at once kill a stacked raid in seconds
-- (walks 15-18). channelInterruptFlags 0x7c2c holds the interrupt bit, so the row is avoid_cast, not a second job
-- for the kicks: every bot that is not the tank the voidwalker is on leaves, and `param` is the 5 yd blast plus 3.
--
-- No Fireball 24611 row: a 0.5 s cast is rarely caught, and the first-castable-wins interrupt scan would spend the
-- kicks Flamestrike needs (class interrupts on the current target already kick Fireballs). No kill_first row: the
-- trash order and a "voidwalkers first" order pull in opposite directions and the walks give no evidence for either.
--
-- Idempotent: the DELETE removes the two bosses' rows, then they go back in. Never edit this file once applied; add a
-- newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (15111, 15146);
INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `comment`) VALUES
(309, 15146, 'avoid_cast', 0, 24614, 0, 8, 'Mad Voidwalker: Consuming Shadows 24614, a 15 s channel; its 24615 hits every enemy within 5 yd (radius index 8) for 400 shadow a second (EventAI 1514601, target within 5 yd)'),
(309, 15111, 'interrupt', 0, 24612, 0, 0, 'Mad Servant: Flamestrike 24612 (castingTimeIndex 5, interruptFlags 15, preventionType 1): 1157-1343 fire in 5 yd of its target point plus 8 s burning ground');
