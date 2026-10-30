---
id: play-features
title: What Bots Can Do
category: play
summary: Catalog of the major bot capabilities, default states, configuration toggles, and engine limits.
tags: [features, capabilities, settings, defaults]
relates_to:
  - play-getting-started
  - play-commands
  - play-known-limitations
  - server-configuration
---

# What Bots Can Do

TortoiseBots brings companion and open-world bots to Tortoise WoW, from party dungeon crawls to an autonomous living world. This page indexes the major bot capabilities, their default states, and the settings that toggle them.

## Your Party (Owned Bots, Hiring, Following, Commands)

| Feature | On by default? | Turned on/off by | Details |
| :--- | :---: | :--- | :--- |
| Spawn and command owned character alts | Always on | None | [Owned alts](commands/roster-and-hiring.md#bot-add) |
| Tactical party actions and orders | Always on | None | [Party actions](commands/party-actions.md) |
| TortoiseBotsManager addon interface (after install) | Always on | None | [TBM addon](commands/addon.md) |
| Geometric follow and combat formations | Always on | None | [Formations](commands/party-actions.md#bot-formation) |
| Direct delegation via chat whispers | Always on | None | [Whisper commands](commands/whispers.md) |
| Recruit full raid rosters with one command (requires raising HireMaxBotsPerPlayer) | No | [`AiPlayerbot.HireMaxBotsPerPlayer`](../server/settings/hiring.md#aiplayerbothiremaxbotsperplayer) | [Raid hires](commands/roster-and-hiring.md#bot-hire-raid) |
| Level grouped owned alts with master | No | [`AiPlayerbot.SyncAltLevelToMaster`](../server/settings/party.md#aiplayerbotsyncaltleveltomaster) | [Owned alts](commands/roster-and-hiring.md#bot-add) |
| Trailing followers mount to catch up | No | [`AiPlayerbot.BoostFollow`](../server/settings/party.md#aiplayerbotboostfollow) | [Follow behavior](commands/party-actions.md#bot-action-follow) |
| Followers hold outside unpulled packs | No | [`AiPlayerbot.DungeonFollowHoldBeforePacks`](../server/settings/party.md#aiplayerbotdungeonfollowholdbeforepacks) | [Dungeon tactics](dungeons-and-raids.md#running-ahead-past-packs) |
| Free instant-teleport in-chat summon action (not `.bot summon`) | No | [`AiPlayerbot.NonGmFreeSummon`](../server/settings/party.md#aiplayerbotnongmfreesummon) | [Summon action](../server/settings/party.md#aiplayerbotnongmfreesummon) |
| Equipment repair and restock maintenance | No | [`AiPlayerbot.OwnedBotMaintenanceEnabled`](../server/settings/party.md#aiplayerbotownedbotmaintenanceenabled) | [Maintenance](commands/roster-and-hiring.md#bot-maintenance) |
| Automatic re-gearing for owned bots | No | [`AiPlayerbot.OwnedBotAutogearEnabled`](../server/settings/party.md#aiplayerbotownedbotautogearenabled) | [Autogear](commands/roster-and-hiring.md#bot-autogear) |
| `.autogear` for real players (GM-only, or self for everyone) | GM-only | [`AiPlayerbot.PlayerAutogearForAll`](../server/settings/party.md#aiplayerbotplayerautogearforall) | [Autogear for players](commands/roster-and-hiring.md#autogear) |
| Teleport owned bots upon group invite | No | [`AiPlayerbot.OwnedBotSummonWhenGroup`](../server/settings/party.md#aiplayerbotownedbotsummonwhengroup) | [Summon on invite](commands/roster-and-hiring.md#automatic-summon-on-group-accept) |
| Group invite summon while in combat | No | [`AiPlayerbot.OwnedBotSummonAllowInCombat`](../server/settings/party.md#aiplayerbotownedbotsummonallowincombat) | [Summon on invite](commands/roster-and-hiring.md#automatic-summon-on-group-accept) |
| Group invite summon while leader dead | No | [`AiPlayerbot.OwnedBotSummonAllowMasterDead`](../server/settings/party.md#aiplayerbotownedbotsummonallowmasterdead) | [Summon on invite](commands/roster-and-hiring.md#automatic-summon-on-group-accept) |
| Group invite summon while bot dead | No | [`AiPlayerbot.OwnedBotSummonAllowBotDead`](../server/settings/party.md#aiplayerbotownedbotsummonallowbotdead) | [Summon on invite](commands/roster-and-hiring.md#automatic-summon-on-group-accept) |
| Automatically revive summoned dead bots | No | [`AiPlayerbot.OwnedBotSummonRevive`](../server/settings/party.md#aiplayerbotownedbotsummonrevive) | [Summon on invite](commands/roster-and-hiring.md#automatic-summon-on-group-accept) |
| Full durability repair on group summon | No | [`AiPlayerbot.OwnedBotSummonRepair`](../server/settings/party.md#aiplayerbotownedbotsummonrepair) | [Summon on invite](commands/roster-and-hiring.md#automatic-summon-on-group-accept) |
| Bots roll Need on empty gear slots | No | [`AiPlayerbot.RollBadItemsWithPlayer`](../server/settings/party.md#aiplayerbotrollbaditemswithplayer) | [Loot rules](dungeons-and-raids.md#loot-rolling-rules) |
| Dismissed alts roam autonomously | No | [`AiPlayerbot.AltBots`](../server/settings/party.md#aiplayerbotaltbots) | [Alt-bots](commands/roster-and-hiring.md#alt-bots-autonomous-alts) |
| Alt-bots log out when owner logs out | No | [`AiPlayerbot.AltBotsLogoutWithOwner`](../server/settings/party.md#aiplayerbotaltbotslogoutwithowner) | [Alt-bots](commands/roster-and-hiring.md#alt-bots-autonomous-alts) |
| Summon owned bots near innkeepers | Yes | [`AiPlayerbot.SummonAtInnkeepersEnabled`](../server/settings/party.md#aiplayerbotsummonatinnkeepersenabled) | [Innkeeper summoning](../server/settings/party.md#aiplayerbotsummonatinnkeepersenabled) |
| Complete the leader's quests when a companion bot turns them in | No | [`AiPlayerbot.SyncQuestForPlayer`](../server/settings/party.md#aiplayerbotsyncquestforplayer) | [Quest syncing](../server/settings/party.md#aiplayerbotsyncquestforplayer) |
| Ready check waits for buffs to refresh | No | [`AiPlayerbot.ForceRebuffOnReadyCheck`](../server/settings/world-population.md#aiplayerbotforcerebuffonreadycheck) | [Ready checks](commands/party-actions.md#bot-action-ready) |
| Remote commander mode for bot parties | No | [`AiPlayerbot.RemoteCommander`](../server/settings/performance.md#aiplayerbotremotecommander) | [Remote commander](commands/commander.md) |
| Mercenary companion hiring at recruiters | Yes | [`AiPlayerbot.HireEnabled`](../server/settings/hiring.md#aiplayerbothireenabled) | [Hiring mercenaries](commands/roster-and-hiring.md#bot-hire) |
| Hire companions anywhere via chat command | No | [`AiPlayerbot.HireAnywhere`](../server/settings/hiring.md#aiplayerbothireanywhere) | [Hire anywhere](commands/roster-and-hiring.md#bot-hire) |
| Static pre-enchanted hire gear tiers | Yes | [`AiPlayerbot.HireGearTiersEnabled`](../server/settings/hiring.md#aiplayerbothiregeartiersenabled) | [Hire gear sets](commands/roster-and-hiring.md#mercenary-contracts) |
| Account-wide gear tier quest unlocks | No | [`AiPlayerbot.HireGearUnlockAccountWide`](../server/settings/hiring.md#aiplayerbothiregearunlockaccountwide) | [Recruiter quests](commands/roster-and-hiring.md#capital-recruiter-services) |
| All gear tier quests offered at once | No | [`AiPlayerbot.HireGearAllQuestsAvailable`](../server/settings/hiring.md#aiplayerbothiregearallquestsavailable) | [Recruiter quests](commands/roster-and-hiring.md#capital-recruiter-services) |
| Purchase world buffs at recruiters | Yes | [`AiPlayerbot.WorldBuffsEnabled`](../server/settings/hiring.md#aiplayerbotworldbuffsenabled) | [Recruiter buffs](commands/roster-and-hiring.md#world-buffs-at-capital-recruiters) |
| Preserve world buffs when entering raids | No | [`AiPlayerbot.WorldBuffsKeepInRaids`](../server/settings/hiring.md#aiplayerbotworldbuffskeepinraids) | [Buffs in raids](commands/roster-and-hiring.md#world-buffs-at-capital-recruiters) |

For hiring fees, summon cooldowns, and follow distances, see [Party settings](../server/settings/party.md) and [Hiring settings](../server/settings/hiring.md).

## Combat & Classes

| Feature | On by default? | Turned on/off by | Details |
| :--- | :---: | :--- | :--- |
| Dedicated rotations for all nine classes | Always on | None | [Class overview](../classes/overview.md) |
| Automatic spec and role detection | Always on | None | [Roles and specs](../classes/overview.md#supported-roles--classes) |
| Coordinated party interrupt casting | Always on | None | [Interrupts](commands/party-actions.md#bot-action-interrupt) |
| Automated dispelling and decursing | Always on | None | None |
| Shared attacker lists among nearby party bots | Yes | [`AiPlayerbot.ShareTargets`](../server/settings/combat.md#aiplayerbotsharetargets) | [Target sharing](../classes/overview.md#shared-bot-behaviors) |
| Ranged weapon pull before melee close | Yes | [`AiPlayerbot.RangedPull`](../server/settings/combat.md#aiplayerbotrangedpull) | [Tank pulling](commands/party-actions.md#bot-action-pull) |
| Hunter Aspect of the Viper on low mana | Yes | [`AiPlayerbot.HunterViperOnLowMana`](../server/settings/class-behaviour.md#aiplayerbothunterviperonlowmana) | [Hunter AI](../classes/hunter.md#turtle-wow-custom-spells) |
| Paladin Judgement coordination on bosses | Yes | [`AiPlayerbot.PaladinJudgementUpkeep`](../server/settings/class-behaviour.md#aiplayerbotpaladinjudgementupkeep) | [Paladin AI](../classes/paladin.md#utility--interrupts) |
| Rogue Feint and Vanish threat drops | Yes | [`AiPlayerbot.RogueDungeonThreatDrops`](../server/settings/class-behaviour.md#aiplayerbotroguedungeonthreatdrops) | [Rogue AI](../classes/rogue.md#utility--interrupts) |
| Warlock demon summons tailored to spec | Yes | [`AiPlayerbot.WarlockPetBySpec`](../server/settings/class-behaviour.md#aiplayerbotwarlockpetbyspec) | [Warlock AI](../classes/warlock.md#utility--interrupts) |
| Healer damage spells and DPS off-healing | Yes | [`AiPlayerbot.EnableOffSpecStrategies`](../server/settings/class-behaviour.md#aiplayerbotenableoffspecstrategies) | [Off-spec rules](../classes/overview.md#healer-off-dps-offdps) |

For health and mana thresholds, engagement distances, and casting pacing, see [Combat settings](../server/settings/combat.md) and [Class behaviour settings](../server/settings/class-behaviour.md).

## Dungeons & Raids

| Feature | On by default? | Turned on/off by | Details |
| :--- | :---: | :--- | :--- |
| Corner line-of-sight pullback pulls | Always on | None | [Pullback tactics](commands/party-actions.md#bot-action-pullback) |
| Crowd control mark protection | Always on | None | [Crowd control](commands/party-actions.md#bot-action-cc) |
| Automated wipe recovery and corpse run | Always on | None | [Wipe recovery](dungeons-and-raids.md#wipe-recovery--instance-navigation) |
| Bomb debuff carriers run out | Always on | None | [Raid mechanics](dungeons-and-raids.md#universal-raid-reactions) |
| Scripted tactics for Vanilla raid encounters | Always on | None | [Vanilla raids](dungeons-and-raids.md#specific-encounter-tactics) |
| Boss strategies for 18 5-player dungeons | Always on | None | [Dungeon reference](dungeon-reference.md) |
| Autonomous dungeon clearing (run leader) | No | [`AiPlayerbot.DungeonSelfClear`](../server/settings/dungeons-raids.md#aiplayerbotdungeonselfclear) | [Dungeon run leader](dungeon-run-leader.md) |
| Steer clear of permanent creature auras | Yes | [`AiPlayerbot.AvoidCreatureAuras`](../server/settings/dungeons-raids.md#aiplayerbotavoidcreatureauras) | [Aura avoidance](dungeons-and-raids.md#universal-raid-reactions) |
| Turtle WoW custom raid encounter tactics | Yes | [`AiPlayerbot.EnableCustomRaidTactics`](../server/settings/dungeons-raids.md#aiplayerbotenablecustomraidtactics) | [Custom raids](dungeons-and-raids.md#custom-turtle-raids) |
| Onyxia whelp priority and egg avoidance | Yes | [`AiPlayerbot.OnyxiaWhelpsAndEggs`](../server/settings/dungeons-raids.md#aiplayerbotonyxiawhelpsandeggs) | [Onyxia tactics](dungeons-and-raids.md#specific-encounter-tactics) |

For bomb dispersal ranges and hazard evasion distances, see [Dungeon & raid settings](../server/settings/dungeons-raids.md); for pull delays, see [Combat settings](../server/settings/combat.md#aiplayerbotpulldpsjoindelay).

## The Living World (Pool Bots Leveling, Questing, Wandering)

| Feature | On by default? | Turned on/off by | Details |
| :--- | :---: | :--- | :--- |
| Autonomous quest pickup, grind, and turn-in | Always on | None | [Questing](../world/living-world.md#open-world-activities) |
| Gathering and crafting professions | Always on | None | [Professions](../world/living-world.md#initial-skills--professions) |
| Physical travel by foot, flight, and boat | Always on | None | [Travel](../world/living-world.md#open-world-activities) |
| Rebuild the pool and re-seed bot levels | No | [`AiPlayerbot.RandomBotPoolReset`](../server/settings/world-population.md#aiplayerbotrandombotpoolreset) | [Bot pool](../server/bot-pool.md) |
| Swift Turtle mount at level 18 | Yes | [`AiPlayerbot.TurtleMountAtLevel`](../server/settings/world-population.md#aiplayerbotturtlemountatlevel) | [Living world](../world/living-world.md#mount-rules) |
| Automated pool bot login on startup | Yes | [`AiPlayerbot.RandomBotAutologin`](../server/settings/world-population.md#aiplayerbotrandombotautologin) | [Bot pool](../server/bot-pool.md) |
| Mass immediate login at startup | No | [`AiPlayerbot.RandomBotLoginAtStartup`](../server/settings/world-population.md#aiplayerbotrandombotloginatstartup) | [Bot pool](../server/bot-pool.md) |
| Auto-create bot accounts if below minimum | Yes | [`AiPlayerbot.RandomBotAutoCreate`](../server/settings/world-population.md#aiplayerbotrandombotautocreate) | [Bot pool](../server/bot-pool.md) |
| Level ladder rotation across level bands | Yes | [`AiPlayerbot.LevelLadder`](../server/settings/world-population.md#aiplayerbotlevelladder) | [Level ladder](../world/living-world.md#level-seed--distribution) |
| Learn racial mounts at levels 40 and 60 | Yes | [`AiPlayerbot.LevelUpMounts`](../server/settings/world-population.md#aiplayerbotlevelupmounts) | [Mounts](../world/living-world.md#mount-rules) |
| Bots may also use shop, cross-race and raid-drop mounts | No | [`AiPlayerbot.RandomBotShopMounts`](../server/settings/world-population.md#aiplayerbotrandombotshopmounts) | [Mounts](../world/living-world.md#mount-rules) |
| Release to nearest graveyard on death | Yes | [`AiPlayerbot.RepopAtGraveyard`](../server/settings/world-population.md#aiplayerbotrepopatgraveyard) | [Graveyard release](../world/living-world.md#autonomous-bot-lifecycle) |
| Avoid pathing near hostile town guards | Yes | [`AiPlayerbot.AvoidHostileTowns`](../server/settings/world-population.md#aiplayerbotavoidhostiletowns) | [Guard avoidance](../world/living-world.md#open-world-activities) |
| Relocate bots caught in hopeless deaths | Yes | [`AiPlayerbot.RelocateHopelessDeaths`](../server/settings/world-population.md#aiplayerbotrelocatehopelessdeaths) | [Death recovery](../world/living-world.md#autonomous-bot-lifecycle) |
| Bind hearthstone within 10 levels of bot | Yes | [`AiPlayerbot.UnstuckHearthLevelFit`](../server/settings/world-population.md#aiplayerbotunstuckhearthlevelfit) | [Hearthstones](../world/living-world.md#autonomous-bot-lifecycle) |
| Teleport stuck bots to travel destination | Yes | [`AiPlayerbot.StuckTravelTeleport`](../server/settings/world-population.md#aiplayerbotstucktravelteleport) | [Stuck recovery](../world/living-world.md#open-world-activities) |
| Autonomous quest log pruning and cleanup | Yes | [`AiPlayerbot.BotQuestLogUpkeep`](../server/settings/world-population.md#aiplayerbotbotquestlogupkeep) | [Quest upkeep](../world/living-world.md#open-world-activities) |
| Migrate bots away from outgrown zones | Yes | [`AiPlayerbot.LeaveOutgrownZones`](../server/settings/world-population.md#aiplayerbotleaveoutgrownzones) | [Zone migration](../world/living-world.md#open-world-activities) |
| Allow bots to spawn in custom isolated starting zones | No | [`AiPlayerbot.AllowIsolatedCustomStartingZones`](../server/settings/world-population.md#aiplayerbotallowisolatedcustomstartingzones) | [Starting zones](../world/living-world.md#level-seeding--starter-progression) |
| Scatter newly seeded bots across world | Yes | [`AiPlayerbot.EnableRandomTeleports`](../server/settings/world-population.md#aiplayerbotenablerandomteleports) | [Level seeding](../world/living-world.md#level-seeding--starter-progression) |
| Distribute creations evenly across zones | Yes | [`AiPlayerbot.RandomBotEvenStartZones`](../server/settings/world-population.md#aiplayerbotrandombotevenstartzones) | [Starter zones](../world/living-world.md#level-seeding--starter-progression) |
| Allow flight paths with repaired client data | No | [`AiPlayerbot.AllowRepairedTaxiPaths`](../server/settings/world-population.md#aiplayerbotallowrepairedtaxipaths) | [Flight paths](../world/living-world.md#open-world-activities) |
| Default masterless bots to wander strategy | Yes | [`AiPlayerbot.UseWanderAsDefaultFollowStrategy`](../server/settings/world-population.md#aiplayerbotusewanderasdefaultfollowstrategy) | [Wandering](../world/living-world.md#open-world-activities) |
| Auto-learn completed class quest spells | Yes | [`AiPlayerbot.AutoLearnQuestSpells`](../server/settings/world-population.md#aiplayerbotautolearnquestspells) | [Class spells](../world/living-world.md#level-seeding--starter-progression) |
| Free trainer spell sweeps on level-up | No | [`AiPlayerbot.AutoLearnTrainerSpells`](../server/settings/world-population.md#aiplayerbotautolearntrainerspells) | [Trainer spells](../world/living-world.md#level-seeding--starter-progression) |
| Free level 60 dungeon and raid spells | No | [`AiPlayerbot.AutoLearnDroppedSpells`](../server/settings/world-population.md#aiplayerbotautolearndroppedspells) | [Book spells](../world/living-world.md#level-seeding--starter-progression) |
| Starter equipment for newly created bots | Yes | [`AiPlayerbot.RandomGearUpgradeEnabled`](../server/settings/world-population.md#aiplayerbotrandomgearupgradeenabled) | [Starter gear](../world/living-world.md#field-kits--equipment-upkeep) |
| Progressive gear upgrades on milestones | Yes | [`AiPlayerbot.RandomGearProgression`](../server/settings/world-population.md#aiplayerbotrandomgearprogression) | [Gear progression](../world/living-world.md#gear-evaluation--upgrades) |
| Automatically equip upgrade drop loot | Yes | [`AiPlayerbot.AutoEquipUpgradeLoot`](../server/settings/world-population.md#aiplayerbotautoequipupgradeloot) | [Loot upgrades](../world/living-world.md#gear-evaluation--upgrades) |
| Allow reputation items on seeded bots | No | [`AiPlayerbot.RandomGearAllowReputation`](../server/settings/world-population.md#aiplayerbotrandomgearallowreputation) | [Gear sources](../world/living-world.md#gear-evaluation--upgrades) |
| Allow PvP honor items on seeded bots | No | [`AiPlayerbot.RandomGearAllowPvP`](../server/settings/world-population.md#aiplayerbotrandomgearallowpvp) | [Gear sources](../world/living-world.md#gear-evaluation--upgrades) |
| Keep all bots active without throttling | Yes | [`AiPlayerbot.DisableActivityPriorities`](../server/settings/performance.md#aiplayerbotdisableactivitypriorities) | [Activity](../server/settings/performance.md#aiplayerbotdisableactivitypriorities) |
| Force full reaction rate near players | No | [`AiPlayerbot.ForceActiveWhenNearPlayer`](../server/settings/performance.md#aiplayerbotforceactivewhennearplayer) | [Proximity](../server/settings/performance.md#aiplayerbotforceactivewhennearplayer) |
| Prevent melee pool bots fleeing in combat | No | [`AiPlayerbot.PoolMeleeHoldGround`](../server/settings/performance.md#aiplayerbotpoolmeleeholdground) | [Combat behavior](../classes/overview.md#shared-bot-behaviors) |

For population counts, XP multipliers, stall timers, and gear quality caps, see [World population settings](../server/settings/world-population.md) and [Performance settings](../server/settings/performance.md).

## Social (Invites, Guilds, Trading)

| Feature | On by default? | Turned on/off by | Details |
| :--- | :---: | :--- | :--- |
| Whisper commands and conversation | Always on | None | [Whisper system](commands/whispers.md) |
| Sharing food, water, and healthstones | Always on | None | [Item trading](../world/social.md#trading-with-pool-bots) |
| Pool bots invite solo questers to party | Yes | [`AiPlayerbot.RandomBotInvitePlayer`](../server/settings/world-population.md#aiplayerbotrandombotinviteplayer) | [Player invites](../world/social.md#grouping--party-invites) |
| Masterless bots form spontaneous groups | No | [`AiPlayerbot.RandomBotGroupNearby`](../server/settings/world-population.md#aiplayerbotrandombotgroupnearby) | [Bot parties](../world/social.md#grouping--party-invites) |
| Group invite broadcasts in chat | Yes | [`AiPlayerbot.InviteChat`](../server/settings/world-population.md#aiplayerbotinvitechat) | [Invite chat](../world/social.md#grouping--party-invites) |
| Bots buy charters and form guilds | Yes | [`AiPlayerbot.RandomBotFormGuild`](../server/settings/world-population.md#aiplayerbotrandombotformguild) | [Bot guilds](../world/social.md#autonomous-guild-formation) |
| Emote greetings when encountering players | Yes | [`AiPlayerbot.EnableGreet`](../server/settings/world-population.md#aiplayerbotenablegreet) | [Greetings](../world/social.md#greetings--emotes) |
| Masterless pool bots greet players | No | [`AiPlayerbot.RandomBotGreet`](../server/settings/world-population.md#aiplayerbotrandombotgreet) | [Greetings](../world/social.md#greetings--emotes) |
| Player trading with masterless pool bots | No | [`AiPlayerbot.PoolBotTradeMode`](../server/settings/world-population.md#aiplayerbotpoolbottrademode) | [Player trading](../world/social.md#trading-with-pool-bots) |
| Automated public chat and announcements | Yes | [`AiPlayerbot.EnableBroadcasts`](../server/settings/chat.md#aiplayerbotenablebroadcasts) | [Chat settings](../server/settings/chat.md#aiplayerbotenablebroadcasts) |
| Thunderfury joke in public chat channels | Yes | [`AiPlayerbot.BroadcastChanceSuggestThunderfury`](../server/settings/chat.md#aiplayerbotbroadcastchancesuggestthunderfury) | [Chat jokes](../server/settings/chat.md#aiplayerbotbroadcastchancesuggestthunderfury) |
| Pool bots speak destinations in /say | No | [`AiPlayerbot.RandomBotSayWithoutMaster`](../server/settings/chat.md#aiplayerbotrandombotsaywithoutmaster) | [Say chat](../server/settings/chat.md#aiplayerbotrandombotsaywithoutmaster) |

For broadcast chances, greeting cooldowns, and guild membership limits, see [Chat settings](../server/settings/chat.md) and [World population settings](../server/settings/world-population.md).

## Economy (Auction House)

| Feature | On by default? | Turned on/off by | Details |
| :--- | :---: | :--- | :--- |
| Vendor trips to sell junk and restock | Always on | None | [Vendor trips](../world/economy.md#vendor-liquidation--repairs) |
| Equipment repairs funded by earned gold | Always on | None | [Repairs](../world/economy.md#vendor-liquidation--repairs) |
| Natural gold income from kills and quests | Always on | None | [Gold economy](../world/economy.md#economic-philosophy) |
| Bots post surplus loot and buy on AH | No | [`AiPlayerbot.AhMarketEnabled`](../server/settings/economy.md#aiplayerbotahmarketenabled) | [Living AH](../world/economy.md#living-auction-house) |
| Synthetic supply for empty auction houses | No | [`AiPlayerbot.AhMarketSyntheticSupply`](../server/settings/economy.md#aiplayerbotahmarketsyntheticsupply) | [Synthetic AH](../world/economy.md#market-administration--drop-tuning) |
| Synthetic buyer orders for player auctions | No | [`AiPlayerbot.AhMarketBuyer`](../server/settings/economy.md#aiplayerbotahmarketbuyer) | [Synthetic buyer](../world/economy.md#market-administration--drop-tuning) |
| Dynamic auction level matching players | No | [`AiPlayerbot.AhMarketDynamicLevel`](../server/settings/economy.md#aiplayerbotahmarketdynamiclevel) | [Dynamic stock](../world/economy.md#market-administration--drop-tuning) |

For market intervals, price variances, vendor batch sizes, and loot rate boosts, see [Economy settings](../server/settings/economy.md).

## Dungeon Finder & Battlegrounds

| Feature | On by default? | Turned on/off by | Details |
| :--- | :---: | :--- | :--- |
| Party joins battlegrounds via group queue | Always on | None | [Party BG queues](../world/queues.md#battlegrounds-with-your-own-party-bots) |
| Objective tactics in WSG, AB, and AV | Always on | None | [Battleground AI](../world/queues.md#battleground-auto-queue-human-demand) |
| LFT party walks to instance portal | Always on | None | [LFT matchmaking](../world/queues.md#looking-for-turtles-lft-dungeon-autofill) |
| Random bots fill vacant human LFT queues | Yes | [`AiPlayerbot.RandomBotLftEnabled`](../server/settings/queues.md#aiplayerbotrandombotlftenabled) | [LFT autofill](../world/queues.md#looking-for-turtles-lft-dungeon-autofill) |
| Borrow and respec bots into missing roles | No | [`AiPlayerbot.RandomBotLftAllowRoleBorrow`](../server/settings/queues.md#aiplayerbotrandombotlftallowroleborrow) | [Role borrowing](../world/queues.md#looking-for-turtles-lft-dungeon-autofill) |
| Inject bots into battleground queues | Yes | [`AiPlayerbot.RandomBotBgEnabled`](../server/settings/queues.md#aiplayerbotrandombotbgenabled) | [Battlegrounds](../world/queues.md#battleground-auto-queue-human-demand) |
| Autonomous bot-only Warsong Gulch matches | Yes | [`AiPlayerbot.RandomBotBgAutonomous`](../server/settings/queues.md#aiplayerbotrandombotbgautonomous) | [Bot matches](../world/queues.md#autonomous-bot-only-matches) |

For queue intervals, fill limits, and bracket instance caps, see [Queue settings](../server/settings/queues.md).

## Server & Monitoring (Dashboard, Logs)

| Feature | On by default? | Turned on/off by | Details |
| :--- | :---: | :--- | :--- |
| In-game bot inspection and status panel | Always on | None | [Bot panel](commands/bot-panel.md) |
| GameMaster pool commands and adoption | Always on | None | [Admin commands](../server/admin-commands.md) |
| Automatic previous-run log preservation | Always on | None | [Log rotation](../server/settings/logging.md#log-rotation--previous-run-logs) |
| Live UDP telemetry stream for dashboard | No | [`AiPlayerbot.Observability`](../server/settings/logging.md#aiplayerbotobservability) | [Dashboard](../server/dashboard.md) |
| Detailed per-bot decision execution logs | No | [`AiPlayerbot.EnableActionLog`](../server/settings/logging.md#aiplayerbotenableactionlog) | [Action logs](../server/settings/logging.md#aiplayerbotenableactionlog) |
| Track cumulative action success/fail counts | No | [`AiPlayerbot.ActionCountsLog`](../server/settings/logging.md#aiplayerbotactioncountslog) | [Action counts](../server/settings/logging.md#aiplayerbotactioncountslog) |
| Restrict detailed action logs to groups | Yes | [`AiPlayerbot.LogInGroupOnly`](../server/settings/performance.md#aiplayerbotlogingrouponly) | [Log scope](../server/settings/logging.md#aiplayerbotactionlogscope) |
| Build item candidate caches on startup | Yes | [`AiPlayerbot.GenerateItemCaches`](../server/settings/performance.md#aiplayerbotgenerateitemcaches) | [Item caches](../server/settings/performance.md#aiplayerbotgenerateitemcaches) |
| Partition travel lookups asynchronously | No | [`AiPlayerbot.AsyncTravelPartitions`](../server/settings/performance.md#aiplayerbotasynctravelpartitions) | [Async travel](../server/settings/performance.md#aiplayerbotasynctravelpartitions) |
| Refresh sensory values at intervals | No | [`AiPlayerbot.ValueRefreshAtInterval`](../server/settings/performance.md#aiplayerbotvaluerefreshatinterval) | [Value refresh](../server/settings/performance.md#aiplayerbotvaluerefreshatinterval) |

For log verbosity, retention limits, CSV streams, and tick budgets, see [Logging settings](../server/settings/logging.md) and [Performance settings](../server/settings/performance.md).

## What Bots Cannot Do (Yet)

- [Self-botting](known-limitations.md#blocked--incomplete-features): you cannot convert your currently logged-in character into an automated bot.
- [Autonomous dungeon clearing](dungeon-run-leader.md): an opt-in option, off by default (`AiPlayerbot.DungeonSelfClear = 0`). Turned on, a bound party clears a stored dungeon plan by itself; its limits are in [Known limitations](known-limitations.md#opt-in-dungeon-clearing).
- [No dedicated 1v1-duel mode](known-limitations.md#blocked--incomplete-features): bots accept duels and fight duel opponents, but lack dedicated competitive player versus player skirmish logic.
- [Expansion features](known-limitations.md#explicit-non-goals): no Death Knights, glyphs, vehicles, Outland zones, or multi-tab guild banks.
- [Pre-movement node queries](known-limitations.md#blocked--incomplete-features): bots cannot check loot rights before walking toward a herb or ore vein.
- [Stair pathfinding desync](known-limitations.md#known-quirks-and-workarounds): pathfinding can desync on steep multi-tiered stairs; use `.bot summon` to snap bots to your location.
- [Dungeon queue teleportation](../world/queues.md#looking-for-turtles-lft-dungeon-autofill): Looking for Turtles forms groups without teleports; bots walk to the instance portal.
