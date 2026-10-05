-- Issue #492: world-buff unlock quests at the six capital <Mercenary Hire> recruiters.
--
-- 14 quests (90000-90013), one per buff per faction. Unlock = rewarded flag
-- (GetQuestRewardStatus): no new table, persists in character_queststatus.
-- Turn-in fee is native: RewOrReqMoney < 0 makes the core block completion
-- without the gold and deduct it at turn-in (200g raid bosses, 100g rest).
-- No XP, no reward items, non-sharable (QuestFlags 8), non-repeatable.
-- Boss quests are Type=62 (raid): Rend/Hakkar use direct kill objectives
-- (core shares them with the group); Rally uses one invisible credit entry
-- 95100 because core requires ALL kill slots, so Ony OR Nef cannot be two
-- direct slots (module translates either kill in PR4). Aura quests (DM any-one
-- of 22817/22818/22820, Sayge any-of-8, Songflower 15366) are event quests
-- (SpecialFlags 2, completed by the module via AreaExploredOrEventHappens in
-- PR5). Silithyst counts 5x invisible credit 95101 via KilledMonsterCredit in
-- PR5. 95100/95101 are never spawned (credit-only creature_template rows).
-- English text only (no locales_quest table on this core). After apply:
-- `.reload quest_template`, `.reload creature_template` (npc_flags below),
-- or restart mangosd. Idempotent: DELETEs the module's quest/credit ranges
-- and the 6 capital relations before re-inserting.

DELETE FROM `quest_template` WHERE `entry` BETWEEN 90000 AND 90013;
DELETE FROM `creature_template` WHERE `entry` IN (95100, 95101);
DELETE FROM `creature_questrelation` WHERE `quest` BETWEEN 90000 AND 90013;
DELETE FROM `creature_involvedrelation` WHERE `quest` BETWEEN 90000 AND 90013;

-- The 6 capital recruiters become quest givers (gossip stays script-owned):
-- 1 = gossip, 2 = quest giver, 3 = both.
UPDATE `creature_template` SET `npc_flags` = 3 WHERE `entry` IN (95017, 95006, 95012, 95025, 95018, 95019);

INSERT INTO `quest_template` (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `MaxLevel`, `QuestLevel`, `Type`, `RequiredClasses`, `RequiredRaces`, `RequiredSkill`, `RequiredSkillValue`, `RequiredCondition`, `RepObjectiveFaction`, `RepObjectiveValue`, `RequiredMinRepFaction`, `RequiredMinRepValue`, `RequiredMaxRepFaction`, `RequiredMaxRepValue`, `SuggestedPlayers`, `LimitTime`, `QuestFlags`, `SpecialFlags`, `PrevQuestId`, `NextQuestId`, `ExclusiveGroup`, `NextQuestInChain`, `SrcItemId`, `SrcItemCount`, `SrcSpell`, `Title`, `Details`, `Objectives`, `OfferRewardText`, `RequestItemsText`, `EndText`, `ObjectiveText1`, `ObjectiveText2`, `ObjectiveText3`, `ObjectiveText4`, `ReqItemId1`, `ReqItemId2`, `ReqItemId3`, `ReqItemId4`, `ReqItemCount1`, `ReqItemCount2`, `ReqItemCount3`, `ReqItemCount4`, `ReqSourceId1`, `ReqSourceId2`, `ReqSourceId3`, `ReqSourceId4`, `ReqSourceCount1`, `ReqSourceCount2`, `ReqSourceCount3`, `ReqSourceCount4`, `ReqCreatureOrGOId1`, `ReqCreatureOrGOId2`, `ReqCreatureOrGOId3`, `ReqCreatureOrGOId4`, `ReqCreatureOrGOCount1`, `ReqCreatureOrGOCount2`, `ReqCreatureOrGOCount3`, `ReqCreatureOrGOCount4`, `ReqSpellCast1`, `ReqSpellCast2`, `ReqSpellCast3`, `ReqSpellCast4`, `RewChoiceItemId1`, `RewChoiceItemId2`, `RewChoiceItemId3`, `RewChoiceItemId4`, `RewChoiceItemId5`, `RewChoiceItemId6`, `RewChoiceItemCount1`, `RewChoiceItemCount2`, `RewChoiceItemCount3`, `RewChoiceItemCount4`, `RewChoiceItemCount5`, `RewChoiceItemCount6`, `RewItemId1`, `RewItemId2`, `RewItemId3`, `RewItemId4`, `RewItemCount1`, `RewItemCount2`, `RewItemCount3`, `RewItemCount4`, `RewRepFaction1`, `RewRepFaction2`, `RewRepFaction3`, `RewRepFaction4`, `RewRepFaction5`, `RewRepValue1`, `RewRepValue2`, `RewRepValue3`, `RewRepValue4`, `RewRepValue5`, `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewSpell`, `RewSpellCast`, `RewMailTemplateId`, `RewMailDelaySecs`, `RewMailMoney`, `PointMapId`, `PointX`, `PointY`, `PointOpt`, `DetailsEmote1`, `DetailsEmote2`, `DetailsEmote3`, `DetailsEmote4`, `DetailsEmoteDelay1`, `DetailsEmoteDelay2`, `DetailsEmoteDelay3`, `DetailsEmoteDelay4`, `IncompleteEmote`, `CompleteEmote`, `OfferRewardEmote1`, `OfferRewardEmote2`, `OfferRewardEmote3`, `OfferRewardEmote4`, `OfferRewardEmoteDelay1`, `OfferRewardEmoteDelay2`, `OfferRewardEmoteDelay3`, `OfferRewardEmoteDelay4`, `StartScript`, `CompleteScript`) VALUES
(90000, 2, 0, 60, 0, 60, 62, 0, 589, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 'Rallying Cry of the Dragonslayer', 'For years the black dragon Onyxia sat at the heart of the Stormwind court wearing the face of Lady Katrana Prestor, and nobody noticed. Embarrassing for everyone involved. When her head finally went up over the city gates, the cheer could be heard in Goldshire.$B$BOur company''s criers have learned that cheer by heart. They will raise it for your company whenever you like, but only behind a true dragonslayer.$B$BSlay Onyxia in her lair, or her brother Nefarian in Blackwing Lair, and return to any of our brokers.', 'Slay Onyxia or Nefarian, then return to a Mercenary Hire broker.', 'A genuine dragonslayer. The criers are beside themselves.$B$BTwo hundred gold, once. Processing fee: the criers, a fresh banner, and a small donation to the Cathedral so the clergy stop complaining about the noise. After that, the Rallying Cry is yours whenever you march.', 'The criers are ready, $N. The dragon, regrettably, is still alive.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 95100, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(90001, 2, 0, 60, 0, 60, 62, 0, 434, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 'Rallying Cry of the Dragonslayer', 'When Onyxia''s head went up over the gates of Orgrimmar, Overlord Runthak bellowed so loud they say the wind riders spooked all the way to the Crossroads. That is the Rallying Cry of the Dragonslayer, $N: the sound of a whole city remembering it is not afraid.$B$BOur company can''t hang a dragon''s head on a pole every time you fancy a fight. But our criers have learned the words, the rhythm, and the part where everyone yells. All they need is a dragonslayer to stand behind.$B$BKill Onyxia in her lair, or her brother Nefarian in Blackwing Lair. Then we talk.', 'Slay Onyxia or Nefarian, then return to a Mercenary Hire broker.', 'A dragonslayer! Stand still, the criers want a look at you.$B$BTwo hundred gold, once, and the cry is yours whenever you need it. That covers the criers, their throat lozenges, and the noise permit from the Orgrimmar guard. Processing fee. Don''t ask about the permit.', 'No dragon blood on your boots yet, $N. The criers are practicing anyway. Loudly.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 95100, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(90002, 2, 0, 60, 0, 60, 62, 0, 589, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 'Warchief''s Blessing', 'Don''t look at me like that, $C. Yes, the Warchief''s Blessing. Yes, that Warchief.$B$BRend Blackhand and his Dark Horde are as much a thorn in Thrall''s side as in ours, and Thrall has let it be known that he honors any blade that ends the pretender. Our company keeps a contact in Orgrimmar, a sensible orc who cares more for gold than for colors. Bring down Rend in Blackrock Spire, and our contact will see the deed properly... recognized.$B$BBest not to mention any of this to Highlord Bolvar.', 'Slay Warchief Rend Blackhand in Blackrock Spire, then return to a Mercenary Hire broker.', 'Rend is dead, and nobody saw you shake hands with an orc. Splendid.$B$BTwo hundred gold settles it: one part for our contact, one part for his silence, and one part for the goblin who swears he has never heard of any of us. Consider it a processing fee. After that, the Warchief''s Blessing is yours whenever you march, and the Light need never know where it came from.', 'Rend still lives, $N. Our friend in Orgrimmar grows impatient, and impatient orcs are bad for business.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10429, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(90003, 2, 0, 60, 0, 60, 62, 0, 434, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 'Warchief''s Blessing', 'Rend Blackhand. Son of Blackhand the Destroyer, and every bit the disappointment his father would have been proud of. He sits atop Blackrock Spire calling himself Warchief, as if the title were something you could steal along with a dragon.$B$BThe true Warchief has a standing offer: bring the pretender down, and his blessing rings out over Orgrimmar. Our company has an arrangement. When one of our own does the deed, that blessing can be... called upon again. For a price, naturally.$B$BClimb the Spire, end the false Warchief and his overgrown pet, and return to any of our brokers. I''ll start on the paperwork.', 'Slay Warchief Rend Blackhand in Blackrock Spire, then return to a Mercenary Hire broker.', 'Rend is dead? Lok''tar! The Warchief will be pleased, and so are we.$B$BNow, about the paperwork. There''s the shaman''s retainer, the runner to Grommash Hold, the scribe''s ink, the tax on the scribe''s ink, and the goblin who audits the scribe. Call it two hundred gold, a one-time processing fee. After that, the Warchief''s Blessing is yours whenever your warband marches out.', 'Still breathing, is he? Rend won''t kill himself, $N. Though with that temper, give him time.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10429, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(90004, 2, 0, 60, 0, 60, 62, 0, 589, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 'Spirit of Zandalar', 'The Zandalar are the oldest of the troll tribes, and they do not impress easily. They sent their priests to Yojamba Isle for one reason: Hakkar the Soulflayer is clawing his way back into the world through Zul''Gurub, and if he succeeds, every troll''s blood becomes his.$B$BMolthor, Hand of Rastakhan, has promised the Spirit of Zandalar to whoever brings the Soulflayer down. Our company has an understanding with the Zandalar. Kill Hakkar, and they will extend that blessing to anyone who marches under your banner.$B$BBring a strong stomach. Zul''Gurub smells exactly as you''d expect.', 'Slay Hakkar the Soulflayer in Zul''Gurub, then return to a Mercenary Hire broker.', 'The Soulflayer is dead. The Zandalar send their thanks, which from them is practically a parade.$B$BTwo hundred gold, once. Processing fee: passage to Yojamba Isle for our envoy, offerings for the loa, and a very nervous goblin to row the boat. After that, the Spirit of Zandalar walks with your warband whenever you ask.', 'The Soulflayer still feeds, $N. The Zandalar are patient, but their patience has an end, and I''d rather not see it.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 14834, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(90005, 2, 0, 60, 0, 60, 62, 0, 434, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 'Spirit of Zandalar', 'The Zandalar are the oldest of the troll tribes, and they do not impress easily. They sent their priests to Yojamba Isle for one reason: Hakkar the Soulflayer is clawing his way back into the world through Zul''Gurub, and if he succeeds, every troll''s blood becomes his.$B$BMolthor, Hand of Rastakhan, has promised the Spirit of Zandalar to whoever brings the Soulflayer down. Our company has an understanding with the Zandalar. Kill Hakkar, and they will extend that blessing to anyone who marches under your banner.$B$BBring a strong stomach. Zul''Gurub smells exactly as you''d expect.', 'Slay Hakkar the Soulflayer in Zul''Gurub, then return to a Mercenary Hire broker.', 'The Soulflayer is dead. The Zandalar send their thanks, which from them is practically a parade.$B$BTwo hundred gold, once. Processing fee: passage to Yojamba Isle for our envoy, offerings for the loa, and a very nervous goblin to row the boat. After that, the Spirit of Zandalar walks with your warband whenever you ask.', 'The Soulflayer still feeds, $N. The Zandalar are patient, but their patience has an end, and I''d rather not see it.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 14834, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(90006, 2, 0, 60, 0, 60, 0, 0, 589, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0, 0, 0, 0, 0, 0, 0, 'Favor of the Gordok', 'The ogres of Dire Maul have exactly two hobbies: smashing and eating. Their guards, Fengus, Mol''dar and Slip''kik, can occasionally be talked into a third, which is sharing, but only with someone the Gordok have learned to respect. Or fear. With ogres it''s hard to tell the difference.$B$BEarn the favor of one of those guards and come back to any of our brokers. Once they know your face, our negotiators can arrange the same treatment for your whole company.', 'Receive the favor of a Gordok guard in Dire Maul, then return to a Mercenary Hire broker.', 'The ogres like you? Remarkable. Our last negotiator came back with teeth marks.$B$BOne hundred gold, once. Call it a processing fee, though between us it mostly pays his healer. After that, our people will see your warband fed, flattered and favored whenever you ask.', 'No ogre has blessed you yet, $N. Try bringing food. Or a bigger club.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(90007, 2, 0, 60, 0, 60, 0, 0, 434, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0, 0, 0, 0, 0, 0, 0, 'Favor of the Gordok', 'The ogres of Dire Maul have exactly two hobbies: smashing and eating. Their guards, Fengus, Mol''dar and Slip''kik, can occasionally be talked into a third, which is sharing, but only with someone the Gordok have learned to respect. Or fear. With ogres it''s hard to tell the difference.$B$BEarn the favor of one of those guards and come back to any of our brokers. Once they know your face, our negotiators can arrange the same treatment for your whole company.', 'Receive the favor of a Gordok guard in Dire Maul, then return to a Mercenary Hire broker.', 'The ogres like you? Remarkable. Our last negotiator came back with teeth marks.$B$BOne hundred gold, once. Call it a processing fee, though between us it mostly pays his healer. After that, our people will see your warband fed, flattered and favored whenever you ask.', 'No ogre has blessed you yet, $N. Try bringing food. Or a bigger club.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(90008, 2, 0, 60, 0, 60, 0, 0, 589, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0, 0, 0, 0, 0, 0, 0, 'Sayge''s Dark Fortune', 'Sayge of the Darkmoon Faire tells fortunes. Good ones, bad ones, and the kind that cling to you for hours. Some say he sees the future. Others say he simply asks the right questions. Either way, he gets paid.$B$BOur company has an arrangement with the Faire, but Sayge will only read for those whose fate he has already seen. Find the Faire, sit in his tent, answer his questions honestly, and come back to us.', 'Receive a fortune from Sayge at the Darkmoon Faire, then return to a Mercenary Hire broker.', 'Sayge sent word ahead. He said you''d be back today, and that you''d pay. He''s very good.$B$BOne hundred gold, once. Processing fee: Silas Darkmoon takes his cut, Sayge takes his, and the rest goes to the gnome who keeps the tent from blowing away. After that, choose your fortune whenever you like.', 'The Faire moves about, $N. Elwynn one week, Mulgore the next. Sayge, apparently, already knows when you''ll turn up.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(90009, 2, 0, 60, 0, 60, 0, 0, 434, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0, 0, 0, 0, 0, 0, 0, 'Sayge''s Dark Fortune', 'Sayge of the Darkmoon Faire tells fortunes. Good ones, bad ones, and the kind that cling to you for hours. Some say he sees the future. Others say he simply asks the right questions. Either way, he gets paid.$B$BOur company has an arrangement with the Faire, but Sayge will only read for those whose fate he has already seen. Find the Faire, sit in his tent, answer his questions honestly, and come back to us.', 'Receive a fortune from Sayge at the Darkmoon Faire, then return to a Mercenary Hire broker.', 'Sayge sent word ahead. He said you''d be back today, and that you''d pay. He''s very good.$B$BOne hundred gold, once. Processing fee: Silas Darkmoon takes his cut, Sayge takes his, and the rest goes to the gnome who keeps the tent from blowing away. After that, choose your fortune whenever you like.', 'The Faire moves about, $N. Elwynn one week, Mulgore the next. Sayge, apparently, already knows when you''ll turn up.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(90010, 2, 0, 60, 0, 60, 0, 0, 589, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0, 0, 0, 0, 0, 0, 0, 'Songflower Serenade', 'Felwood was beautiful once. Now the demons'' corruption has seeped into everything, even the songflowers that used to sing to anyone passing beneath them. The Cenarion Circle has a salve that can cleanse a sick flower, if only for a little while, and a cleansed songflower''s serenade lifts the spirit like nothing else.$B$BOur herbalists want to bottle that song, so to speak, and they need someone who has heard it. Cleanse a songflower in Felwood, stand in its blessing, and report back.', 'Receive the blessing of a cleansed songflower in Felwood, then return to a Mercenary Hire broker.', 'You''re humming. That''s the song, all right.$B$BOne hundred gold, once. Processing fee: salve, seeds, and the herbalists'' hazard pay, since Felwood keeps trying to eat them. After that, the Songflower Serenade is yours whenever you ask.', 'You don''t hum like someone who has heard a songflower, $N. Felwood is waiting. Mind the satyrs.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(90011, 2, 0, 60, 0, 60, 0, 0, 434, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0, 0, 0, 0, 0, 0, 0, 'Songflower Serenade', 'Felwood was beautiful once. Now the demons'' corruption has seeped into everything, even the songflowers that used to sing to anyone passing beneath them. The Cenarion Circle has a salve that can cleanse a sick flower, if only for a little while, and a cleansed songflower''s serenade lifts the spirit like nothing else.$B$BOur herbalists want to bottle that song, so to speak, and they need someone who has heard it. Cleanse a songflower in Felwood, stand in its blessing, and report back.', 'Receive the blessing of a cleansed songflower in Felwood, then return to a Mercenary Hire broker.', 'You''re humming. That''s the song, all right.$B$BOne hundred gold, once. Processing fee: salve, seeds, and the herbalists'' hazard pay, since Felwood keeps trying to eat them. After that, the Songflower Serenade is yours whenever you ask.', 'You don''t hum like someone who has heard a songflower, $N. Felwood is waiting. Mind the satyrs.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(90012, 2, 0, 60, 0, 60, 62, 0, 589, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 'Traces of Silithyst', 'Silithus is sand, bugs, and more sand. It also bleeds silithyst, a crystal dust both armies would kill for. The Cenarion Circle wants it studied, the Horde wants it stolen, and we want it in our quartermaster''s crates.$B$BThe Horde keeps sending raiding parties into the desert to snatch the dust from under our noses. Teach five of them better manners, then come back. Our quartermasters will handle the rest.', 'Defeat 5 Horde champions in Silithus, then return to a Mercenary Hire broker.', 'Five! The sand is a little redder and our crates a little fuller.$B$BOne hundred gold, once. Processing fee: crates, gryphon freight, and a footman to sweep the sand out of the crates. After that, our quartermasters will dust your warband with silithyst before every march.', 'The Horde still walks those sands, $N. Five of them. The bugs don''t count.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 95101, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
(90013, 2, 0, 60, 0, 60, 62, 0, 434, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 'Traces of Silithyst', 'Silithus is sand, bugs, and more sand. It also bleeds silithyst, a crystal dust both armies would kill for. The Cenarion Circle wants it studied, the Alliance wants it stolen, and we want it in our quartermaster''s crates.$B$BThe Alliance keeps sending raiding parties into the desert to snatch the dust from under our noses. Teach five of them better manners, then come back. Our quartermasters will handle the rest.', 'Defeat 5 Alliance champions in Silithus, then return to a Mercenary Hire broker.', 'Five! The sand is a little redder and our crates a little fuller.$B$BOne hundred gold, once. Processing fee: crates, wind rider freight, and a grunt to sweep the sand out of the crates. After that, our quartermasters will dust your warband with silithyst before every march.', 'The Alliance still walks those sands, $N. Five of them. The bugs don''t count.', '', '', '', '', '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 95101, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1000000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

-- Invisible kill-credit entries (never spawned; the module grants credit by
-- entry id). Minimal template rows: neutral critters, no gossip, no loot.
INSERT INTO `creature_template` (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `rank`, `type`, `type_flags`, `script_name`) VALUES
(95100, 0, 'Dragonslayer Credit', '', 60, 60, 100, 100, 35, 0, 0, 7, 0, ''),
(95101, 0, 'Silithyst Credit', '', 60, 60, 100, 100, 35, 0, 0, 7, 0, '');

-- Starters + enders: each faction's quests on its 3 capital recruiters.
-- Alliance: SW 95017, IF 95006, Darnassus 95012. Horde: Org 95025, UC 95018, TB 95019.
INSERT INTO `creature_questrelation` (`id`, `quest`) VALUES
(95017, 90000),
(95006, 90000),
(95012, 90000),
(95025, 90001),
(95018, 90001),
(95019, 90001),
(95017, 90002),
(95006, 90002),
(95012, 90002),
(95025, 90003),
(95018, 90003),
(95019, 90003),
(95017, 90004),
(95006, 90004),
(95012, 90004),
(95025, 90005),
(95018, 90005),
(95019, 90005),
(95017, 90006),
(95006, 90006),
(95012, 90006),
(95025, 90007),
(95018, 90007),
(95019, 90007),
(95017, 90008),
(95006, 90008),
(95012, 90008),
(95025, 90009),
(95018, 90009),
(95019, 90009),
(95017, 90010),
(95006, 90010),
(95012, 90010),
(95025, 90011),
(95018, 90011),
(95019, 90011),
(95017, 90012),
(95006, 90012),
(95012, 90012),
(95025, 90013),
(95018, 90013),
(95019, 90013);
INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES
(95017, 90000),
(95006, 90000),
(95012, 90000),
(95025, 90001),
(95018, 90001),
(95019, 90001),
(95017, 90002),
(95006, 90002),
(95012, 90002),
(95025, 90003),
(95018, 90003),
(95019, 90003),
(95017, 90004),
(95006, 90004),
(95012, 90004),
(95025, 90005),
(95018, 90005),
(95019, 90005),
(95017, 90006),
(95006, 90006),
(95012, 90006),
(95025, 90007),
(95018, 90007),
(95019, 90007),
(95017, 90008),
(95006, 90008),
(95012, 90008),
(95025, 90009),
(95018, 90009),
(95019, 90009),
(95017, 90010),
(95006, 90010),
(95012, 90010),
(95025, 90011),
(95018, 90011),
(95019, 90011),
(95017, 90012),
(95006, 90012),
(95012, 90012),
(95025, 90013),
(95018, 90013),
(95019, 90013);
