-- Issue #492 follow-up: the world-buff unlock quests (90000-90013) had
-- ZoneOrSort = 0, which the 1.18.1 client shows as "Missing header! (quest
-- designers)" in the quest log. Quest log headers come from the client's
-- QuestSort.dbc, so a module cannot add its own; file them under the stock
-- "Special" category (QuestSort 284 -> ZoneOrSort -284). Idempotent.
UPDATE `quest_template` SET `ZoneOrSort` = -284 WHERE `entry` BETWEEN 90000 AND 90013;

-- Objective lines: the quest log showed the invisible credit creature
-- ("Dragonslayer Credit slain: 0/1"). ObjectiveText replaces that line with
-- what the player actually has to do. Rend/Hakkar already use the boss
-- itself as the objective, so they read correctly as they are.
UPDATE `quest_template` SET `ObjectiveText1` = 'Onyxia or Nefarian slain' WHERE `entry` IN (90000, 90001);
UPDATE `quest_template` SET `ObjectiveText1` = 'Horde player slain in Silithus' WHERE `entry` = 90012;
UPDATE `quest_template` SET `ObjectiveText1` = 'Alliance player slain in Silithus' WHERE `entry` = 90013;
-- The credit entries also name themselves after the objective, in case
-- anything else prints the creature name.
UPDATE `creature_template` SET `name` = 'Onyxia or Nefarian' WHERE `entry` = 95100;
UPDATE `creature_template` SET `name` = 'Enemy player in Silithus' WHERE `entry` = 95101;

-- The turn-in fee was only visible at turn-in. Name it in the objective
-- summary, which the quest window shows before accepting and the quest log
-- keeps showing (the fee itself stays the negative RewOrReqMoney).
UPDATE `quest_template` SET `Objectives` = REPLACE(`Objectives`, 'then return to a Mercenary Hire broker.', 'then return to a Mercenary Hire broker with the 200 gold processing fee.')
WHERE `entry` BETWEEN 90000 AND 90013 AND `RewOrReqMoney` = -2000000;
UPDATE `quest_template` SET `Objectives` = REPLACE(`Objectives`, 'then return to a Mercenary Hire broker.', 'then return to a Mercenary Hire broker with the 100 gold processing fee.')
WHERE `entry` BETWEEN 90000 AND 90013 AND `RewOrReqMoney` = -1000000;
