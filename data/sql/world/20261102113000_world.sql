-- Bots no longer chat on their own (no replies, announcements or suggestions):
-- drop the texts only that chatter used, and the speech-chance table nothing
-- reads any more. Functional texts (command answers, quest and loot replies to
-- the master) stay.
DELETE FROM `ai_playerbot_texts`
WHERE `name` LIKE 'broadcast\_%'
   OR `name` LIKE 'suggest\_%'
   OR `name` IN ('reply', 'thunderfury_spam', 'join_group', 'join_raid',
                 'response_lfg_quests_channel', 'response_lfg_quests_whisper',
                 'response_wtb_items_channel', 'response_wtb_items_whisper',
                 'string_unknown_area', 'string_empty_link');

DROP TABLE IF EXISTS `ai_playerbot_texts_chance`;
