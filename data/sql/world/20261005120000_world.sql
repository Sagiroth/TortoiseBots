-- Issue #492 follow-up: the world-buff unlock quests (90000-90013) had
-- ZoneOrSort = 0, which the 1.18.1 client shows as "Missing header! (quest
-- designers)" in the quest log. Quest log headers come from the client's
-- QuestSort.dbc, so a module cannot add its own; file them under the stock
-- "Special" category (QuestSort 284 -> ZoneOrSort -284). Idempotent.
UPDATE `quest_template` SET `ZoneOrSort` = -284 WHERE `entry` BETWEEN 90000 AND 90013;
