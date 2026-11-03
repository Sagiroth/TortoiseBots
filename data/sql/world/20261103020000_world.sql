-- Boss tactics: Zul'Gurub's aura rows (dispel_priority, freeze_on_aura, flee_on_aura, dps_hold_after), the Shade
-- bot_aura gate, Gahz'ranka's interrupt hold and the Edge of Madness interrupts.
--
-- Every id below is from the core (src/scripts/dungeons/zulgurub, sql/base/tw_world_spell_template.sql):
--   Hakkar 14834: Poisonous Blood 24321 (Son of Hakkar 11357 casts 24320; 24321 dispel 4) is what damages Hakkar
--     under Blood Siphon 24324, so it must never be cured: the row is dispel_priority mode never. The siphon itself
--     is instant with interruptFlags 0, so its old interrupt row is deleted again here. Aspect of Jeklik 24687
--     (silence, magic) and Aspect of Venoxis 24688 (poison) are cured in that order; Cause Insanity 24327 charms the
--     victim and restores its threat afterwards, so DPS waits for the tank (until_tank, 4 s).
--   Mandokir 11382: Threatening Gaze 24314 demands standing still (no 2 yd move, no 500 threat), so it is
--     freeze_on_aura, not a run-out. Gaze picks a random non-top attacker, so the tank must own the boss before DPS
--     (until_tank, 4 s). Ohgan 14988 enrages Mandokir when it dies first, and is summoned onto Mandokir's victim:
--     hold_kill on Ohgan while Mandokir lives (Mandokir dies first).
--   Jin'do 11380: Hex 17172 (magic, cast on the tank) is cured at once; Delusions 24306 (curse) is never removed
--     because only cursed players can damage a Shade 14986; the Shade's kill_first row is gated on that aura, so
--     only cursed DPS switch to it. Brain Wash Totem 15112 is killed first (its Avoidance aura makes it immune to
--     area damage); the Healing Ward 14987 moves to priority 2.
--   Mar'li 14510: a Spawn of Mar'li 15041 is killed first (it levels up every 3 s); Corrosive Poison 24111 and
--     Poison Bolt Volley 24099 (both poison) are cured, the volley at priority 2. Drain Life 24300's existing
--     interrupt row now fires: the scan reads the channel slot.
--   Arlokk 14515: Mark of Arlokk 24210 sends every Zulian Prowler at the marked player, so the carrier walks to the
--     main tank (flee_on_aura mode to_tank, 5 yd); Shadow Word: Pain 24212 is cured at low priority. No row for
--     Vanish.
--   Gahz'ranka 15114: Frost Breath 16099 is a cone (behind, 20 yd), Massive Geyser 22421 is interruptible and resets
--     all threat at the cast start, so it is interrupted and DPS is held 3 s after the cast.
--   Gri'lek 15082: Avatar 24646 drops the victim's threat by 50 % and retargets, so DPS waits 3 s.
--   Hazza'rah 15083: Nightmare Illusion 15163 is killed first.
--   Renataki 15084: he vanishes, reappears elsewhere and drops threat; the blink trigger holds DPS 3 s.
--   Wushoolay 15085: Chain Lightning 24680 (creature_template's inline spell list 24680/24682/24683, the
--     spell-list AI casts it; castingTimeIndex 16, interruptFlags 15) is
--     interrupted. Lightning Cloud is ground avoidance, not a row.
--
-- Idempotent: every DELETE is scoped to the boss and kind whose rows this file writes (so an unrelated row of the
-- same boss, Mar'li's Drain Life interrupt, Arlokk's Cleave row, Jin'do's other kinds, stays), and the dead Hakkar
-- interrupt key is deleted again. Never edit this file once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14834) AND `kind` = 'dispel_priority';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14834) AND `kind` = 'dps_hold_after';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14834) AND `kind` = 'interrupt' AND `target_entry` = 0 AND `spell_id` = 24324;
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (11382) AND `kind` = 'freeze_on_aura';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (11382) AND `kind` = 'dps_hold_after';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14988) AND `kind` = 'hold_kill';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (11380) AND `kind` = 'dispel_priority';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (11380) AND `kind` = 'kill_first';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14510) AND `kind` = 'kill_first';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14510) AND `kind` = 'dispel_priority';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14515) AND `kind` = 'flee_on_aura';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (14515) AND `kind` = 'dispel_priority';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (15114) AND `kind` = 'behind';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (15114) AND `kind` = 'interrupt';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (15114) AND `kind` = 'dps_hold_after';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (15082) AND `kind` = 'dps_hold_after';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (15083) AND `kind` = 'kill_first';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (15084) AND `kind` = 'dps_hold_after';
DELETE FROM `ai_boss_tactic` WHERE `map` = 309 AND `boss_entry` IN (15085) AND `kind` = 'interrupt';

INSERT INTO `ai_boss_tactic` (`map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `bot_aura`, `tactic_mode`, `comment`) VALUES
(309, 14834, 'dispel_priority', 0, 24321, 0, 0, 0, 'never', 'Hakkar: Poisonous Blood 24321 must stay (it damages Hakkar under Blood Siphon 24324; no cure of type poison may remove it)'),
(309, 14834, 'dispel_priority', 0, 24687, 1, 0, 0, 'now', 'Hakkar: Aspect of Jeklik 24687 silence, cured first'),
(309, 14834, 'dispel_priority', 0, 24688, 2, 0, 0, 'now', 'Hakkar: Aspect of Venoxis 24688 poison DoT, cured after the silence and never on a 24321 carrier'),
(309, 14834, 'dps_hold_after', 0, 0, 0, 4, 0, 'until_tank', 'Hakkar: Cause Insanity 24327 charms the victim and restores its threat; DPS waits for the tank (4 s, capped)'),
(309, 11382, 'freeze_on_aura', 0, 24314, 0, 0, 0, '', 'Mandokir: Threatening Gaze 24314 forbids moving more than 2 yd or gaining 500 threat; the carrier stands still and casts nothing'),
(309, 11382, 'dps_hold_after', 0, 0, 0, 4, 0, 'until_tank', 'Mandokir: the Gaze picks a random non-top attacker; DPS waits for the tank (4 s, capped)'),
(309, 14988, 'hold_kill', 11382, 0, 0, 0, 0, '', 'Ohgan: no DPS while Mandokir 11382 lives (Ohgan dying first enrages Mandokir)'),
(309, 11380, 'dispel_priority', 0, 17172, 0, 0, 0, 'now', 'Jindo: Hex 17172 (magic, on the tank) is cured at once'),
(309, 11380, 'dispel_priority', 0, 24306, 0, 0, 0, 'never', 'Jindo: Delusions 24306 must stay (only cursed players can damage a Shade 14986)'),
(309, 11380, 'kill_first', 14986, 0, 0, 0, 24306, '', 'Jindo: a Shade 14986 is attacked only by a bot carrying Delusions 24306'),
(309, 11380, 'kill_first', 15112, 0, 1, 0, 0, '', 'Jindo: Brain Wash Totem 15112 first (it charms a random attacker; Avoidance makes it immune to area damage)'),
(309, 11380, 'kill_first', 14987, 0, 2, 0, 0, '', 'Jindo: Powerfull Healing Ward 14987 after the totem'),
(309, 14510, 'kill_first', 15041, 0, 1, 0, 0, '', 'Marli: Spawn of Marli 15041 first (it levels up every 3 s)'),
(309, 14510, 'dispel_priority', 0, 24111, 1, 0, 0, 'now', 'Marli: Corrosive Poison 24111 on the tank, cured first'),
(309, 14510, 'dispel_priority', 0, 24099, 2, 0, 0, 'now', 'Marli: Poison Bolt Volley 24099 poison, cured after the tank aura (the volley is instant, so no interrupt)'),
(309, 14515, 'flee_on_aura', 0, 24210, 0, 5, 0, 'to_tank', 'Arlokk: Mark of Arlokk 24210 sends every Zulian Prowler at the carrier; walk to the main tank (5 yd)'),
(309, 14515, 'dispel_priority', 0, 24212, 3, 0, 0, 'now', 'Arlokk: Shadow Word: Pain 24212 on the tank, low priority'),
(309, 15114, 'behind', 0, 16099, 0, 20, 0, '', 'Gahzranka: Frost Breath 16099 cone, stay out of the front (20 yd)'),
(309, 15114, 'interrupt', 0, 22421, 0, 0, 0, '', 'Gahzranka: Massive Geyser 22421 (castingTimeIndex 16, interruptFlags 15)'),
(309, 15114, 'dps_hold_after', 0, 22421, 0, 3, 0, '', 'Gahzranka: Massive Geyser resets all threat at the cast start; hold DPS 3 s after it'),
(309, 15082, 'dps_hold_after', 0, 24646, 0, 3, 0, '', 'Grilek: Avatar 24646 drops the victim threat by 50 % and retargets; hold DPS 3 s'),
(309, 15083, 'kill_first', 15163, 0, 1, 0, 0, '', 'Hazzarah: Nightmare Illusion 15163 frees the slept player, kill it first'),
(309, 15084, 'dps_hold_after', 0, 0, 0, 3, 0, 'blink', 'Renataki: vanish/ambush teleports him and drops threat; hold DPS 3 s on the blink'),
(309, 15085, 'interrupt', 0, 24680, 0, 0, 0, '', 'Wushoolay: Chain Lightning 24680 (creature_template spell list 24680/24682/24683, castingTimeIndex 16, interruptFlags 15)');
