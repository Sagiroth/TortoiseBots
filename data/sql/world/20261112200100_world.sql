-- Onyxia's Lair (map 249), walk 2h: burn Onyxia first, drop the Hatcher kill order.
--
-- Walk 2g evidence: the two Hatchers 50144 lost only 9-25 % in 56-83 s of P3 (466k health together) while P1 takes
-- 39 % off Onyxia in 83-97 s, so the kill-first order spent the phase's damage on 466k of adds and killed nobody.
-- A held Hatcher stays on its tank (g2-a-1: 21 of 26 frames), so the `hold_kill` row can go with it: the damage
-- dealers fight her from the first pull and the tanks hold the Hatchers. With the slot-0 tank_post row (D1) she is
-- held at E, and the 50143 gate_hold row still holds the damage dealers off her while Cindarion fights.
--
-- Idempotent: both DELETEs name exactly the two rows this file removes; Cindarion's kill_first stays. Never edit
-- once applied; add a newer migration.

DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 10184 AND `kind` = 'hold_kill' AND `target_entry` = 50144;
DELETE FROM `ai_boss_tactic` WHERE `map` = 249 AND `boss_entry` = 50144 AND `kind` = 'kill_first';
