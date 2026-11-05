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

How to read the tables:

- **On?** — whether a fresh server has it switched on. "Always" means there is no switch.
- **Setting** — the line in the server's `aiplayerbot.conf` that changes it. Only the person who runs the server can change it.
- **More** — where it is explained.

New here? Read [Getting Started](getting-started.md) first.

## Your Party (Owned Bots, Hiring, Following, Commands)

### Your own alts

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| Log in your alts as bots and command them | Always | — | [`.bot add`](commands/roster-and-hiring.md#bot-add) |
| An alt that leaves your group keeps playing on its own (alt-bot) | Yes | [`AiPlayerbot.AltBots`](../server/settings/party.md#aiplayerbotaltbots) | [Alt-bots](commands/roster-and-hiring.md#alt-bots-autonomous-alts) |
| Alt-bots log out when you log out | No | [`AiPlayerbot.AltBotsLogoutWithOwner`](../server/settings/party.md#aiplayerbotaltbotslogoutwithowner) | [Alt-bots](commands/roster-and-hiring.md#alt-bots-autonomous-alts) |
| Alts in your group level up to your level | No | [`AiPlayerbot.SyncAltLevelToMaster`](../server/settings/party.md#aiplayerbotsyncaltleveltomaster) | [Getting started](getting-started.md#levelling-spells-and-gear) |
| `.bot maintenance`: refill reagents, ammo, food and repair | No | [`AiPlayerbot.OwnedBotMaintenanceEnabled`](../server/settings/party.md#aiplayerbotownedbotmaintenanceenabled) | [Maintenance](commands/roster-and-hiring.md#bot-maintenance) |
| `.bot autogear`: dress a bot in level-fitting gear | No | [`AiPlayerbot.OwnedBotAutogearEnabled`](../server/settings/party.md#aiplayerbotownedbotautogearenabled) | [Autogear](commands/roster-and-hiring.md#bot-autogear) |
| `.autogear` for your own player character | GM only | [`AiPlayerbot.PlayerAutogearForAll`](../server/settings/party.md#aiplayerbotplayerautogearforall) | [Autogear for players](commands/roster-and-hiring.md#autogear) |

### Hired companions

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| Hire companions at `<Mercenary Hire>` recruiters | Yes | [`AiPlayerbot.HireEnabled`](../server/settings/hiring.md#aiplayerbothireenabled) | [Hiring](commands/roster-and-hiring.md#bot-hire) |
| Hire with the `.bot hire` command anywhere | No | [`AiPlayerbot.HireAnywhere`](../server/settings/hiring.md#aiplayerbothireanywhere) | [Hiring](commands/roster-and-hiring.md#bot-hire) |
| Hire a whole raid in one go (needs hiring anywhere on, and more than 4 hires allowed) | No | [`AiPlayerbot.HireAnywhere`](../server/settings/hiring.md#aiplayerbothireanywhere), [`AiPlayerbot.HireMaxBotsPerPlayer`](../server/settings/hiring.md#aiplayerbothiremaxbotsperplayer) | [Raid hires](commands/roster-and-hiring.md#bot-hire-raid) |
| Level 60 hires in fixed gear tier sets | Yes | [`AiPlayerbot.HireGearTiersEnabled`](../server/settings/hiring.md#aiplayerbothiregeartiersenabled) | [Mercenary contracts](commands/roster-and-hiring.md#mercenary-contracts) |
| Gear tier unlocks count for your whole account | No | [`AiPlayerbot.HireGearUnlockAccountWide`](../server/settings/hiring.md#aiplayerbothiregearunlockaccountwide) | [Recruiter services](commands/roster-and-hiring.md#capital-recruiter-services) |
| All gear tier quests offered at once | No | [`AiPlayerbot.HireGearAllQuestsAvailable`](../server/settings/hiring.md#aiplayerbothiregearallquestsavailable) | [Recruiter services](commands/roster-and-hiring.md#capital-recruiter-services) |
| Hires loot and roll on loot | No | [`AiPlayerbot.HireLoot`](../server/settings/hiring.md#aiplayerbothireloot) | [Loot rules](dungeons-and-raids.md#loot-rolling-rules) |
| Hires come along into battlegrounds | No | [`AiPlayerbot.HireAllowPvP`](../server/settings/hiring.md#aiplayerbothireallowpvp) | [Battlegrounds](../world/queues.md#battlegrounds-with-your-own-party-bots) |
| Buy world buffs at capital recruiters | Yes | [`AiPlayerbot.WorldBuffsEnabled`](../server/settings/hiring.md#aiplayerbotworldbuffsenabled) | [World buffs](commands/roster-and-hiring.md#world-buffs-at-capital-recruiters) |
| Keep world buffs when entering a raid | No | [`AiPlayerbot.WorldBuffsKeepInRaids`](../server/settings/hiring.md#aiplayerbotworldbuffskeepinraids) | [World buffs](commands/roster-and-hiring.md#world-buffs-at-capital-recruiters) |

### Guild bots

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| Invite a pool bot into your guild and command it like your own alt (hires, your own alts, full guilds and queued bots excluded) | Always | — | [Guild bots](getting-started.md#guild-bots) |
| Order guild bots to farm, buy or craft items for the guild bank | Yes | [`AiPlayerbot.AutoDoQuests`](../server/settings/world-population.md#aiplayerbotautodoquests) | [Guild orders](guild-orders.md) |

### Orders, following and summoning

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| Party orders: attack, pull, stay, follow, CC, focus | Always | — | [Party actions](commands/party-actions.md) |
| Orders by whisper | Always | — | [Whisper commands](commands/whispers.md) |
| The `/tbm` addon (once you install it) | Always | — | [Addon](commands/addon.md) |
| Follow formations | Always | — | [Formations](commands/party-actions.md#bot-formation) |
| Followers far behind mount up to catch up | Yes | [`AiPlayerbot.BoostFollow`](../server/settings/party.md#aiplayerbotboostfollow) | [Follow](commands/party-actions.md#bot-action-follow) |
| Followers far behind stop before packs you skipped | No | [`AiPlayerbot.DungeonFollowHoldBeforePacks`](../server/settings/party.md#aiplayerbotdungeonfollowholdbeforepacks) | [Dungeon tactics](dungeons-and-raids.md#running-ahead-past-packs) |
| Summon your bots near an innkeeper | Yes | [`AiPlayerbot.SummonAtInnkeepersEnabled`](../server/settings/party.md#aiplayerbotsummonatinnkeepersenabled) | [Setting](../server/settings/party.md#aiplayerbotsummonatinnkeepersenabled) |
| Bots that accept your invite are summoned to you | No | [`AiPlayerbot.OwnedBotSummonWhenGroup`](../server/settings/party.md#aiplayerbotownedbotsummonwhengroup) | [Summon on invite](commands/roster-and-hiring.md#automatic-summon-on-group-accept) |
| ...also in combat / while you are dead / while the bot is dead | No | [`InCombat`](../server/settings/party.md#aiplayerbotownedbotsummonallowincombat), [`MasterDead`](../server/settings/party.md#aiplayerbotownedbotsummonallowmasterdead), [`BotDead`](../server/settings/party.md#aiplayerbotownedbotsummonallowbotdead) | [Summon on invite](commands/roster-and-hiring.md#automatic-summon-on-group-accept) |
| ...and revived / repaired on arrival | No | [`Revive`](../server/settings/party.md#aiplayerbotownedbotsummonrevive), [`Repair`](../server/settings/party.md#aiplayerbotownedbotsummonrepair) | [Summon on invite](commands/roster-and-hiring.md#automatic-summon-on-group-accept) |
| Free instant summon by whispering a bot (not `.bot summon`) | No | [`AiPlayerbot.NonGmFreeSummon`](../server/settings/party.md#aiplayerbotnongmfreesummon) | [Setting](../server/settings/party.md#aiplayerbotnongmfreesummon) |
| Your quest completes when your companion turns it in | No | [`AiPlayerbot.SyncQuestForPlayer`](../server/settings/party.md#aiplayerbotsyncquestforplayer) | [Setting](../server/settings/party.md#aiplayerbotsyncquestforplayer) |
| Ready check waits until buffs are refreshed | No | [`AiPlayerbot.ForceRebuffOnReadyCheck`](../server/settings/world-population.md#aiplayerbotforcerebuffonreadycheck) | [Ready check](commands/party-actions.md#bot-action-ready) |
| Bots Need on items for empty gear slots | No | [`AiPlayerbot.RollBadItemsWithPlayer`](../server/settings/party.md#aiplayerbotrollbaditemswithplayer) | [Loot rules](dungeons-and-raids.md#loot-rolling-rules) |
| Command a bot-only party from anywhere (remote commander) | No | [`AiPlayerbot.RemoteCommander`](../server/settings/performance.md#aiplayerbotremotecommander) | [Remote commander](commands/commander.md) |

## Combat & Classes

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| Rotations for all nine classes, spec and role picked from talents | Always | — | [Class overview](../classes/overview.md) |
| Bots share interrupts and dispels | Always | — | [Interrupts](commands/party-actions.md#bot-action-interrupt) |
| Party bots help each other against the same attackers | Yes | [`AiPlayerbot.ShareTargets`](../server/settings/combat.md#aiplayerbotsharetargets) | [Shared behaviour](../classes/overview.md#shared-bot-behaviors) |
| Tank pulls with a ranged weapon before closing in | Yes | [`AiPlayerbot.RangedPull`](../server/settings/combat.md#aiplayerbotrangedpull) | [Pull](commands/party-actions.md#bot-action-pull) |
| Healers deal damage and damage dealers heal when it is quiet | Yes | [`AiPlayerbot.EnableOffSpecStrategies`](../server/settings/class-behaviour.md#aiplayerbotenableoffspecstrategies) | [Off-spec rules](../classes/overview.md#healer-off-dps-offdps) |
| Hunter: Aspect of the Viper when low on mana | Yes | [`AiPlayerbot.HunterViperOnLowMana`](../server/settings/class-behaviour.md#aiplayerbothunterviperonlowmana) | [Hunter](../classes/hunter.md#turtle-wow-custom-spells) |
| Paladin: keep Judgements up on bosses | Yes | [`AiPlayerbot.PaladinJudgementUpkeep`](../server/settings/class-behaviour.md#aiplayerbotpaladinjudgementupkeep) | [Paladin](../classes/paladin.md#utility--interrupts) |
| Rogue: Feint and Vanish to drop threat | Yes | [`AiPlayerbot.RogueDungeonThreatDrops`](../server/settings/class-behaviour.md#aiplayerbotroguedungeonthreatdrops) | [Rogue](../classes/rogue.md#utility--interrupts) |
| Warlock: demon picked by spec | Yes | [`AiPlayerbot.WarlockPetBySpec`](../server/settings/class-behaviour.md#aiplayerbotwarlockpetbyspec) | [Warlock](../classes/warlock.md#utility--interrupts) |
| Warlock: DoTs and Drain Soul skip targets that are about to die | No | [`AiPlayerbot.WarlockRaidRotation`](../server/settings/class-behaviour.md#aiplayerbotwarlockraidrotation) | [Warlock](../classes/warlock.md#class-settings) |

## Dungeons & Raids

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| Pullback: tank pulls and runs back to you around a corner | Always | — | [Pullback](commands/party-actions.md#bot-action-pullback) |
| Crowd control marks are respected | Always | — | [Crowd control](commands/party-actions.md#bot-action-cc) |
| Bots release, run back and revive after a wipe | Always | — | [Wipe recovery](dungeons-and-raids.md#wipe-recovery--instance-navigation) |
| Raid boss tactics (Molten Core to Naxxramas) | Always | — | [Raid tactics](dungeons-and-raids.md#specific-encounter-tactics) |
| Bomb carriers run out of the raid | Always | — | [Raid reactions](dungeons-and-raids.md#universal-raid-reactions) |
| Turtle raid tactics (Emerald Sanctum, Karazhan) | Yes | [`AiPlayerbot.EnableCustomRaidTactics`](../server/settings/dungeons-raids.md#aiplayerbotenablecustomraidtactics) | [Custom raids](dungeons-and-raids.md#custom-turtle-raids) |
| Onyxia: kill whelps, avoid eggs | Yes | [`AiPlayerbot.OnyxiaWhelpsAndEggs`](../server/settings/dungeons-raids.md#aiplayerbotonyxiawhelpsandeggs) | [Onyxia](dungeons-and-raids.md#specific-encounter-tactics) |
| Stay out of permanent boss auras | Yes | [`AiPlayerbot.AvoidCreatureAuras`](../server/settings/dungeons-raids.md#aiplayerbotavoidcreatureauras) | [Raid reactions](dungeons-and-raids.md#universal-raid-reactions) |
| Bots walk a stored dungeon route by themselves (run leader, work in progress) | Yes | [`AiPlayerbot.DungeonSelfClear`](../server/settings/dungeons-raids.md#aiplayerbotdungeonselfclear) | [Run leader](dungeon-run-leader.md) |
| 5-man boss rules (what to interrupt, which add first) for normal parties; run-leader and commander parties use them anyway | No | [`AiPlayerbot.DungeonTactics`](../server/settings/dungeons-raids.md#aiplayerbotdungeontactics) | [Boss tactics](boss-tactics.md) |

## The Living World (Pool Bots Leveling, Questing, Wandering)

### What pool bots do

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| Quest, grind and turn in quests | Always | — | [Living world](../world/living-world.md#open-world-activities) |
| Gather herbs, ore and skins; fish | Always | — | [Professions](../world/living-world.md#initial-skills--professions) |
| Travel on foot, by flight path and boat | Always | — | [Travel](../world/living-world.md#open-world-activities) |
| Move on to new zones when a zone gets too easy | Yes | [`AiPlayerbot.LeaveOutgrownZones`](../server/settings/world-population.md#aiplayerbotleaveoutgrownzones) | [Living world](../world/living-world.md#open-world-activities) |
| Keep the quest log tidy | Yes | [`AiPlayerbot.BotQuestLogUpkeep`](../server/settings/world-population.md#aiplayerbotbotquestlogupkeep) | [Living world](../world/living-world.md#open-world-activities) |
| Avoid walking past enemy town guards | Yes | [`AiPlayerbot.AvoidHostileTowns`](../server/settings/world-population.md#aiplayerbotavoidhostiletowns) | [Living world](../world/living-world.md#open-world-activities) |
| Wander around when they have nothing to do | Yes | [`AiPlayerbot.UseWanderAsDefaultFollowStrategy`](../server/settings/world-population.md#aiplayerbotusewanderasdefaultfollowstrategy) | [Living world](../world/living-world.md#open-world-activities) |

### Levelling, spells and gear

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| Learn class trainer spells for free on level-up (pool bots and hires) | Yes | [`AiPlayerbot.AutoLearnTrainerSpells`](../server/settings/world-population.md#aiplayerbotautolearntrainerspells) | [Class spells](../world/living-world.md#class-spells) |
| Learn class quest spells for free | Yes | [`AiPlayerbot.AutoLearnQuestSpells`](../server/settings/world-population.md#aiplayerbotautolearnquestspells) | [Class spells](../world/living-world.md#class-spells) |
| Your alts learn class quest spells for free | No | [`AiPlayerbot.AutoLearnQuestSpellsAlts`](../server/settings/world-population.md#aiplayerbotautolearnquestspellsalts) | [Class spells](../world/living-world.md#class-spells) |
| Hunter and warlock pets train themselves on level-up (pool bots and hires) | Yes | [`AiPlayerbot.PetAutoTrain`](../server/settings/world-population.md#aiplayerbotpetautotrain) | [Class spells](../world/living-world.md#class-spells) |
| Your alts' pets train themselves on level-up | No | [`AiPlayerbot.PetAutoTrainAlts`](../server/settings/world-population.md#aiplayerbotpetautotrainalts) | [Class spells](../world/living-world.md#class-spells) |
| Learn spells from dungeon and raid books at 60 | No | [`AiPlayerbot.AutoLearnDroppedSpells`](../server/settings/world-population.md#aiplayerbotautolearndroppedspells) | [Class spells](../world/living-world.md#class-spells) |
| Starter outfit for new bots | Yes | [`AiPlayerbot.RandomGearUpgradeEnabled`](../server/settings/world-population.md#aiplayerbotrandomgearupgradeenabled) | [Starter gear](../world/living-world.md#field-kits--equipment-upkeep) |
| Gear upgrades at level milestones | Yes | [`AiPlayerbot.RandomGearProgression`](../server/settings/world-population.md#aiplayerbotrandomgearprogression) | [Gear](../world/living-world.md#gear-evaluation--upgrades) |
| Equip better gear they loot | Yes | [`AiPlayerbot.AutoEquipUpgradeLoot`](../server/settings/world-population.md#aiplayerbotautoequipupgradeloot) | [Gear](../world/living-world.md#gear-evaluation--upgrades) |
| Reputation and PvP items in their gear | No | [`AiPlayerbot.RandomGearAllowReputation`](../server/settings/world-population.md#aiplayerbotrandomgearallowreputation), [`AiPlayerbot.RandomGearAllowPvP`](../server/settings/world-population.md#aiplayerbotrandomgearallowpvp) | [Gear](../world/living-world.md#gear-evaluation--upgrades) |
| Swift Riding Turtle at level 18 | Yes | [`AiPlayerbot.TurtleMountAtLevel`](../server/settings/world-population.md#aiplayerbotturtlemountatlevel) | [Mounts](../world/living-world.md#mount-rules) |
| Racial mounts at levels 40 and 60 | Yes | [`AiPlayerbot.LevelUpMounts`](../server/settings/world-population.md#aiplayerbotlevelupmounts) | [Mounts](../world/living-world.md#mount-rules) |
| Shop, other-race and raid-drop mounts too | No | [`AiPlayerbot.RandomBotShopMounts`](../server/settings/world-population.md#aiplayerbotrandombotshopmounts) | [Mounts](../world/living-world.md#mount-rules) |

### Death and getting unstuck

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| Release to the nearest graveyard | Yes | [`AiPlayerbot.RepopAtGraveyard`](../server/settings/world-population.md#aiplayerbotrepopatgraveyard) | [Living world](../world/living-world.md#autonomous-bot-lifecycle) |
| Move bots that keep dying in a too-hard zone | Yes | [`AiPlayerbot.RelocateHopelessDeaths`](../server/settings/world-population.md#aiplayerbotrelocatehopelessdeaths) | [Living world](../world/living-world.md#autonomous-bot-lifecycle) |
| Teleport a stuck bot to where it was going | Yes | [`AiPlayerbot.StuckTravelTeleport`](../server/settings/world-population.md#aiplayerbotstucktravelteleport) | [Living world](../world/living-world.md#open-world-activities) |
| Set the hearthstone to an inn near the bot's level | Yes | [`AiPlayerbot.UnstuckHearthLevelFit`](../server/settings/world-population.md#aiplayerbotunstuckhearthlevelfit) | [Living world](../world/living-world.md#autonomous-bot-lifecycle) |
| Melee pool bots never flee in combat | No | [`AiPlayerbot.PoolMeleeHoldGround`](../server/settings/performance.md#aiplayerbotpoolmeleeholdground) | [Shared behaviour](../classes/overview.md#shared-bot-behaviors) |

### How the pool is filled (server admins)

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| Create bot accounts and log the pool in at startup | Yes | [`RandomBotAutoCreate`](../server/settings/world-population.md#aiplayerbotrandombotautocreate), [`RandomBotAutologin`](../server/settings/world-population.md#aiplayerbotrandombotautologin), [`RandomBotLoginAtStartup`](../server/settings/world-population.md#aiplayerbotrandombotloginatstartup) | [Bot pool](../server/bot-pool.md) |
| New bots start spread over levels 5-60 | Yes | [`RandomBotStartLevelMin`/`Max`](../server/settings/world-population.md#aiplayerbotrandombotstartlevelmin--randombotstartlevelmax) | [Level seed](../world/living-world.md#level-seed--distribution) |
| Online bots spread evenly over level bands | Yes | [`AiPlayerbot.LevelLadder`](../server/settings/world-population.md#aiplayerbotlevelladder) | [Level seed](../world/living-world.md#level-seed--distribution) |
| New bots spread evenly over starting zones | Yes | [`AiPlayerbot.RandomBotEvenStartZones`](../server/settings/world-population.md#aiplayerbotrandombotevenstartzones) | [Starting zones](../world/living-world.md#level-seeding--starter-progression) |
| New bots from level 10 start in a zone that fits their level | Yes | [`AiPlayerbot.EnableRandomTeleports`](../server/settings/world-population.md#aiplayerbotenablerandomteleports) | [Starting zones](../world/living-world.md#level-seeding--starter-progression) |
| Bots also start in the isolated custom starting zones | No | [`AiPlayerbot.AllowIsolatedCustomStartingZones`](../server/settings/world-population.md#aiplayerbotallowisolatedcustomstartingzones) | [Starting zones](../world/living-world.md#level-seeding--starter-progression) |
| Use flight paths that need repaired client data | No | [`AiPlayerbot.AllowRepairedTaxiPaths`](../server/settings/world-population.md#aiplayerbotallowrepairedtaxipaths) | [Travel](../world/living-world.md#open-world-activities) |
| Rebuild the pool and re-roll levels | No | [`AiPlayerbot.RandomBotPoolReset`](../server/settings/world-population.md#aiplayerbotrandombotpoolreset) | [Bot pool](../server/bot-pool.md) |
| All bots stay fully active (no throttling) | Yes | [`AiPlayerbot.DisableActivityPriorities`](../server/settings/performance.md#aiplayerbotdisableactivitypriorities) | [Setting](../server/settings/performance.md#aiplayerbotdisableactivitypriorities) |
| Bots near players always react at full speed | No | [`AiPlayerbot.ForceActiveWhenNearPlayer`](../server/settings/performance.md#aiplayerbotforceactivewhennearplayer) | [Setting](../server/settings/performance.md#aiplayerbotforceactivewhennearplayer) |

## Social (Invites, Guilds, Trading)

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| Pool bots invite you to their group (not while you are `/dnd`) | Yes | [`AiPlayerbot.RandomBotInvitePlayer`](../server/settings/world-population.md#aiplayerbotrandombotinviteplayer) | [Invites](../world/social.md#grouping--party-invites) |
| Pool bots group with each other | No | [`AiPlayerbot.RandomBotGroupNearby`](../server/settings/world-population.md#aiplayerbotrandombotgroupnearby) | [Invites](../world/social.md#grouping--party-invites) |
| Pool bots join your guild when invited (hires, your own alts, full guilds and queued bots excluded) | Always | — | [Guilds](../world/social.md#guilds) |
| Bots found guilds of their own | No | [`AiPlayerbot.RandomBotFormGuild`](../server/settings/world-population.md#aiplayerbotrandombotformguild) | [Guilds](../world/social.md#guilds) |
| Companion mages conjure food and water and trade it to group members who have none | Yes | — | [Trading](../world/social.md#trading-with-pool-bots) |
| Your companions greet players they meet | Yes | [`AiPlayerbot.EnableGreet`](../server/settings/world-population.md#aiplayerbotenablegreet) | [Greetings](../world/social.md#greetings--emotes) |
| Pool bots greet players too | No | [`AiPlayerbot.RandomBotGreet`](../server/settings/world-population.md#aiplayerbotrandombotgreet) | [Greetings](../world/social.md#greetings--emotes) |
| Trade with pool bots that are not yours | No | [`AiPlayerbot.PoolBotTradeMode`](../server/settings/world-population.md#aiplayerbotpoolbottrademode) | [Trading](../world/social.md#trading-with-pool-bots) |

## Economy (Auction House)

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| Bots sell junk and repair at vendors | Always | — | [Vendors](../world/economy.md#vendor-liquidation--repairs) |
| Bots earn their gold from kills, quests and sales | Always | — | [Economy](../world/economy.md#economic-philosophy) |
| Bots sell their loot on the auction house | Yes | [`AiPlayerbot.AhMarketEnabled`](../server/settings/economy.md#aiplayerbotahmarketenabled) | [Auction house](../world/economy.md#living-auction-house) |
| Bots buy upgrades on the auction house with their own gold | Yes | [`AiPlayerbot.AhMarketBuyer`](../server/settings/economy.md#aiplayerbotahmarketbuyer) | [Auction house](../world/economy.md#living-auction-house) |
| Made-up listings for an empty auction house | No | [`AiPlayerbot.AhMarketSyntheticSupply`](../server/settings/economy.md#aiplayerbotahmarketsyntheticsupply) | [Admin tuning](../world/economy.md#market-administration--drop-tuning) |
| Made-up listings follow the players' levels | No | [`AiPlayerbot.AhMarketDynamicLevel`](../server/settings/economy.md#aiplayerbotahmarketdynamiclevel) | [Admin tuning](../world/economy.md#market-administration--drop-tuning) |

## Dungeon Finder & Battlegrounds

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| Pool bots fill missing roles when you queue for a dungeon (LFT) | Yes | [`AiPlayerbot.RandomBotLftEnabled`](../server/settings/queues.md#aiplayerbotrandombotlftenabled) | [LFT](../world/queues.md#looking-for-turtles-lft-dungeon-autofill) |
| Bots respec into a missing role for LFT | No | [`AiPlayerbot.RandomBotLftAllowRoleBorrow`](../server/settings/queues.md#aiplayerbotrandombotlftallowroleborrow) | [LFT](../world/queues.md#looking-for-turtles-lft-dungeon-autofill) |
| An LFT group walks to the dungeon together | Always | — | [LFT](../world/queues.md#looking-for-turtles-lft-dungeon-autofill) |
| Pool bots fill battleground queues when players queue | Yes | [`AiPlayerbot.RandomBotBgEnabled`](../server/settings/queues.md#aiplayerbotrandombotbgenabled) | [Battlegrounds](../world/queues.md#battleground-auto-queue-human-demand) |
| Bot-only Warsong Gulch matches when no player queues | Yes | [`AiPlayerbot.RandomBotBgAutonomous`](../server/settings/queues.md#aiplayerbotrandombotbgautonomous) | [Bot matches](../world/queues.md#autonomous-bot-only-matches) |
| Your alts join a battleground with you (Join as Group; hires stay out unless allowed in) | Always | — | [Your party in BGs](../world/queues.md#battlegrounds-with-your-own-party-bots) |
| Bots play the objectives in WSG, AB and AV | Always | — | [Battleground AI](../world/queues.md#battleground-auto-queue-human-demand) |

## Server & Monitoring (Dashboard, Logs)

For server admins.

| Feature | On? | Setting | More |
| :--- | :---: | :--- | :--- |
| In-game bot panel and status | Always | — | [Bot panel](commands/bot-panel.md) |
| GameMaster pool commands | Always | — | [Admin commands](../server/admin-commands.md) |
| Web dashboard feed | Yes | [`AiPlayerbot.Observability`](../server/settings/logging.md#aiplayerbotobservability) | [Dashboard](../server/dashboard.md) |
| Per-bot action logs (kept 3 days) | Yes | [`AiPlayerbot.EnableActionLog`](../server/settings/logging.md#aiplayerbotenableactionlog) | [Action logs](../server/settings/logging.md#aiplayerbotenableactionlog) |
| Action logs only for bots in a group | Yes | [`AiPlayerbot.LogInGroupOnly`](../server/settings/performance.md#aiplayerbotlogingrouponly) | [Log scope](../server/settings/logging.md#aiplayerbotactionlogscope) |
| Count action successes and failures | No | [`AiPlayerbot.ActionCountsLog`](../server/settings/logging.md#aiplayerbotactioncountslog) | [Setting](../server/settings/logging.md#aiplayerbotactioncountslog) |
| Logs of the previous run are kept | Always | — | [Log rotation](../server/settings/logging.md#log-rotation--previous-run-logs) |
| Build item caches at startup | Yes | [`AiPlayerbot.GenerateItemCaches`](../server/settings/performance.md#aiplayerbotgenerateitemcaches) | [Setting](../server/settings/performance.md#aiplayerbotgenerateitemcaches) |
| Travel lookups in the background | No | [`AiPlayerbot.AsyncTravelPartitions`](../server/settings/performance.md#aiplayerbotasynctravelpartitions) | [Setting](../server/settings/performance.md#aiplayerbotasynctravelpartitions) |
| Refresh bot values on a timer | No | [`AiPlayerbot.ValueRefreshAtInterval`](../server/settings/performance.md#aiplayerbotvaluerefreshatinterval) | [Setting](../server/settings/performance.md#aiplayerbotvaluerefreshatinterval) |

## What Bots Cannot Do (Yet)

| Missing | What to do instead |
| :--- | :--- |
| Clear most instances by themselves: the run leader is work in progress and only a few routes are checked | Lead the instance yourself. [Details](known-limitations.md#dungeon-run-leader-work-in-progress) |
| A real duel mode: bots accept duels but fight them like any fight | — |
| Check a herb or ore node before walking to it | Nothing; the bot walks on after finding it empty. [Details](known-limitations.md#blocked--incomplete-features) |
| Climb steep, many-floored stairs reliably | `.bot summon <Name>` out of combat. [Details](known-limitations.md#known-quirks-and-workarounds) |
