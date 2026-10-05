-- Issue #492 follow-up: the world-buff unlock quests (90000-90013) were
-- inserted with QuestFlags = 8, meant as "non-sharable", but 8 is
-- QUEST_FLAGS_SHARABLE, so a player could push them to the group. They are
-- per-character purchases: no flags. Bots already refuse them at accept
-- (IsBannedQuest), this stops the share prompt as well. Idempotent.
UPDATE `quest_template` SET `QuestFlags` = 0 WHERE `entry` BETWEEN 90000 AND 90013;
