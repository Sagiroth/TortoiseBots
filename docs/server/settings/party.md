---
id: server-settings-party
title: Party & Quality-of-Life Settings
category: server
summary: Configuration settings for player-owned bots, alt-bots, follow behavior, summoning, and party loot.
tags: [config, party, alts, qol]
relates_to:
  - play-commands-party-actions
  - server-settings-hiring
---

# Party & Quality-of-Life Settings

These settings configure player-owned bots, alt-bots, formation positioning, and party convenience options. Settings go in `aiplayerbot.conf`; see [How configuration works](../configuration.md).

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.SyncAltLevelToMaster`](#aiplayerbotsyncaltleveltomaster) | `0` | Automatically levels grouped owned bot alts to match the leader level. |
| [`AiPlayerbot.BoostFollow`](#aiplayerbotboostfollow) | `1` | Causes bots trailing far behind to mount up to catch up. |
| [`AiPlayerbot.DungeonFollowHoldBeforePacks`](#aiplayerbotdungeonfollowholdbeforepacks) | `0` | Stops follower bots outside unpulled pack aggro radius (attack detection range). |
| [`AiPlayerbot.DefaultFormation`](#aiplayerbotdefaultformation) | `chaos` | Sets default idle follow formation around the party leader. |
| [`AiPlayerbot.RaidFollowWedge`](#aiplayerbotraidfollowwedge) | `0` | Spreads raid and dungeon followers behind the leader and has far stragglers walk the leader's path. |
| [`AiPlayerbot.NonGmFreeSummon`](#aiplayerbotnongmfreesummon) | `0` | Enables the free instant summon action for non-staff players. |
| [`AiPlayerbot.OwnedBotMaintenanceEnabled`](#aiplayerbotownedbotmaintenanceenabled) | `0` | Enables the maintenance command for owned and hired companion bots. |
| [`AiPlayerbot.OwnedBotAutogearEnabled`](#aiplayerbotownedbotautogearenabled) | `0` | Enables automated incremental re-gearing for owned and hired bots. |
| [`AiPlayerbot.OwnedBotAutogearQualityCap`](#aiplayerbotownedbotautogearqualitycap) | `2` | Caps the maximum item quality allowed during automatic re-gearing. |
| [`AiPlayerbot.OwnedBotAutogearIlvlCap`](#aiplayerbotownedbotautogearilvlcap) | `0` | Caps the maximum item level allowed during automatic re-gearing. |
| [`AiPlayerbot.PlayerAutogearForAll`](#aiplayerbotplayerautogearforall) | `0` | Lets every player run `.autogear` on themselves instead of GMs only. |
| [`AiPlayerbot.OwnedBotSummonWhenGroup`](#aiplayerbotownedbotsummonwhengroup) | `0` | Automatically teleports owned bots to leader when accepting group invite. |
| [`AiPlayerbot.OwnedBotSummonAllowInCombat`](#aiplayerbotownedbotsummonallowincombat) | `0` | Allows group accept summoning while either leader or bot is in combat. |
| [`AiPlayerbot.OwnedBotSummonAllowMasterDead`](#aiplayerbotownedbotsummonallowmasterdead) | `0` | Allows group accept summoning while the party leader is dead. |
| [`AiPlayerbot.OwnedBotSummonAllowBotDead`](#aiplayerbotownedbotsummonallowbotdead) | `0` | Allows group accept summoning while the invited bot is dead. |
| [`AiPlayerbot.OwnedBotSummonRevive`](#aiplayerbotownedbotsummonrevive) | `0` | Revives a dead bot before teleporting it on group accept. |
| [`AiPlayerbot.OwnedBotSummonRepair`](#aiplayerbotownedbotsummonrepair) | `0` | Automatically repairs bot durability upon group accept summon arrival. |
| [`AiPlayerbot.OwnedBotSummonCooldown`](#aiplayerbotownedbotsummoncooldown) | `300` | Minimum seconds required between group accept summons per bot. |
| [`AiPlayerbot.RollBadItemsWithPlayer`](#aiplayerbotrollbaditemswithplayer) | `0` | Forces bots to roll Need on dungeon drops for empty gear slots. |
| [`AiPlayerbot.AltBots`](#aiplayerbotaltbots) | `1` | Allows dismissed owned alts to continue roaming and playing independently. |
| [`AiPlayerbot.AltBotsLogoutWithOwner`](#aiplayerbotaltbotslogoutwithowner) | `0` | Forces active alt-bots to log out when their account owner logs out. |
| [`AiPlayerbot.SummonAtInnkeepersEnabled`](#aiplayerbotsummonatinnkeepersenabled) | `1` (built in) | Enables summoning owned bots while near an innkeeper NPC. |
| [`AiPlayerbot.SyncQuestForPlayer`](#aiplayerbotsyncquestforplayer) | `0` (built in) | Completes quests for player master when a companion bot turns them in. |
| [`AiPlayerbot.SyncQuestWithPlayer`](#aiplayerbotsyncquestwithplayer) | `0` | Completes quests for bots when the player master turns them in. |
| [`AiPlayerbot.BotCheats`](#aiplayerbotbotcheats) | `repair,breath` | Convenience and sustenance cheats granted to owned companion bots and alts. |

## Settings

### `AiPlayerbot.SyncAltLevelToMaster`
Automatically levels owned bot alts to match your character level while grouped. Enable this if you want your alts to stay viable party companions without manual grinding.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

While an owned (non-random) bot is in your group and you are the group leader, it gains one character level per AI update until its level equals your level.

This applies only to owned (non-random) bots, never to random pool bots or hired mercenaries.

</details>

### `AiPlayerbot.BoostFollow`
Causes bots that have fallen far behind the party leader to mount up and ride to catch up. Enable this to prevent followers from straggling on long journeys.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When a follower bot falls farther behind the leader than its configured reaction distance (150 yards), the bot evaluates its mount state. If eligible, it mounts up and rides until it reaches standard follow distance.

This behavior uses standard movement speeds and is safe around combat.

</details>

### `AiPlayerbot.DungeonFollowHoldBeforePacks`
Prevents follower bots from running through unpulled monster packs when trailing behind their leader in dungeons. Enable this for cleaner dungeon navigation.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Inside a dungeon instance, while the bot is not in combat, a bot whose leader is more than 30 yards ahead calculates its own travel path along the route. The leader may be fighting: a bot far behind still stops before the packs on its way to that fight (the pack the leader fights is already in combat and never holds the bot).

If an unpulled hostile creature stands along that path, the bot stops 3 yards outside the creature aggro radius and holds its position rather than running through. The bot whispers its master once per pack (at most every 30 seconds): `Pack in the way: <name>. Waiting here - pull it or come back for me.`

While holding, the follow action keeps the bot standing there. It resumes moving once the pack is defeated, when the route changes, or when the leader returns within 25 yards. Only creatures on the bot's sight list count, and paths are rechecked once per second. This check is suspended while the bot or the leader is on a transport.

</details>

### `AiPlayerbot.DefaultFormation`
Sets the default idle spatial arrangement party bots form around the leader. Change this if you prefer a tighter or more structured marching order.
**Default:** `chaos` · **Values:** `near`, `melee`, `queue`, `chaos`, `circle`, `line`, `shield`, `arrow`, `far` (also accepts `default`, `custom`) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Idle party bots arrange themselves in the chosen geometric formation relative to the group leader orientation:
- `chaos`: loose cluster around the leader with slight randomized offsets to avoid unnatural stacking.
- `near`: tight following radius within melee interaction range.
- `melee`: follow formation keyed to the master's target at follow range.
- `far`: follow formation holding the follow angle at follow range.
- `circle`: evenly spaced ring surrounding the leader.
- `line`: horizontal line abreast with the leader.
- `shield`: role-aware lines with tanks holding forward of the leader and the rest grouped behind.
- `arrow`: arrowhead formation with role groups staged in depth behind the leader's facing direction.
- `queue`: single-file column trailing behind the leader.

Bots then stand in their slot and follow the leader normally.

Individual bots can be assigned custom formations during gameplay using the [`.bot formation`](../../play/commands/party-actions.md#bot-formation) command.

</details>

### `AiPlayerbot.RaidFollowWedge`
Inside a dungeon or raid, followers stand in a 150 degree arc directly behind the group leader instead of around it, and a straggler walks the leader's own path.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Inside a dungeon or raid, while the bot is in a group, the configured formation gives way to a `near` formation spread over a 150 degree wedge behind the leader. No slot is ahead of or beside the leader, so a move row that passes a pack no longer puts a follower in front of the tank. With more than ten followers the wedge uses a second ring three yards further back; tanks and healers stay on the inner ring.

The follow distance in the wedge is `AiPlayerbot.RaidFollowDistance` (4 yd). A follower more than 20 yards behind the leader, while neither of them is in combat, walks the leader's recorded recent positions instead of the direct route to its slot, so stairs and corners are taken the way the leader took them.

The open world, and a bot whose player chose a formation with [`.bot formation`](../../play/commands/party-actions.md#bot-formation), keep the configured formation. `0` keeps the configured formation everywhere.

</details>

### `AiPlayerbot.NonGmFreeSummon`
Allows non-staff players to use the free instant summon action to teleport bots directly to their location. Enable this on private realms for fast party regrouping.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, players without GM permissions can execute the in-chat summon action to teleport party bots straight to them.

Without this setting, bots must travel physically, use meeting stones, or rely on innkeeper travel mechanics. This setting leaves `.bot summon <Name>` with its own ownership and state checks.

</details>

### `AiPlayerbot.OwnedBotMaintenanceEnabled`
Enables the comprehensive maintenance command for owned and hired companions. Use this to let players easily replenish reagents and repairs on their bots.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, players can execute `.bot maintenance <Name>` to perform full upkeep on their owned bots or hired companions. Upkeep restocks reagents, potions, food, consumables, bandages and ammunition (including thrown), and fully repairs durability.

When disabled, attempting to run the command informs the player that maintenance is disabled. This command cannot be used on random pool bots.

</details>

### `AiPlayerbot.OwnedBotAutogearEnabled`
Enables automatic incremental equipment upgrades for owned bots and hired companions. Enable this to reduce manual inventory management for your alts.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Allows players to run `.bot autogear <Name> [quality|ilvl]` to equip level-appropriate items directly from candidate pools.

When enabled, the bot selects items fitting its class and spec stat weightings up to the configured item level and quality caps. Off by default; pool bots do not use this command.

</details>

### `AiPlayerbot.OwnedBotAutogearQualityCap`
Caps the maximum item quality that can be granted when running the autogear command. Adjust this to prevent bots from generating overly powerful gear.
**Default:** `2` · **Values:** `0` poor, `1` common, `2` uncommon (green), `3` rare (blue), `4` epic (purple), `5` legendary · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Restricts candidate equipment selected by `.bot autogear` to the specified item quality tier or lower.

Quality keywords supplied in player commands are clamped to this ceiling. The default of `2` restricts autogear to uncommon (green) items.

</details>

### `AiPlayerbot.OwnedBotAutogearIlvlCap`
Caps the maximum item level of equipment that can be granted when running the autogear command. Set this to match your server progression tier.
**Default:** `0` · **Values:** `0` (no cap) or maximum item level · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Limits candidate equipment selected by `.bot autogear` to items at or below the specified item level.

When set to `0`, no item level cap is enforced and candidate equipment is bounded only by the bot character level and quality cap. Explicit item level arguments in player commands are clamped to this ceiling.

</details>

### `AiPlayerbot.PlayerAutogearForAll`
Opens `.autogear` — which gears a real player the way a spawned bot gets geared — beyond the GameMaster staff.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

`.autogear` equips a real player with items picked from the same gear pool, scoring and level/quality limits a spawned bot of the same level receives. It only upgrades: replaced items go to the target's bags, or to their own mailbox when the bags are full.

- `0` (default): only GameMasters can run `.autogear`, on any real online player including themselves.
- `1`: every player can run `.autogear` on themselves only. Naming another player is refused.

This flag never changes bot gearing: owned bots keep `.bot autogear`, controlled by [`AiPlayerbot.OwnedBotAutogearEnabled`](#aiplayerbotownedbotautogearenabled). Characters on a Turtle challenge mode (Hardcore, Slow & Steady, ...) are GM-only for `.autogear` whatever this flag is set to.

</details>

### `AiPlayerbot.OwnedBotSummonWhenGroup`
Automatically teleports an owned bot or hired companion to the leader when accepting a group invitation. Enable this to assemble your team instantly.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When an owned bot or hired companion accepts an invitation into your group while located far away (beyond sight distance on the same map), it queues an async summon to your location. Cross-map joins stay manual.

This rule applies only to owned bots and hired mercenaries, never to random pool bots. Additional conditions (combat, death, and cooldowns) are governed by the companion summon settings below.

</details>

### `AiPlayerbot.OwnedBotSummonAllowInCombat`
Controls whether group accept summoning can fire while either the leader or the bot is engaged in combat. Enable with caution.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `1`, group accept summons will teleport the bot even if the player or bot is currently in combat.

When set to `0`, active combat blocks the instant teleport upon group join.

</details>

### `AiPlayerbot.OwnedBotSummonAllowMasterDead`
Controls whether group accept summoning can fire while the group leader is dead.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `1`, group accept summoning teleports the bot even while the leader is dead.

When set to `0`, the leader must be alive for group accept summoning to trigger.

</details>

### `AiPlayerbot.OwnedBotSummonAllowBotDead`
Controls whether group accept summoning can teleport an invited bot that is currently dead. Pair with revive settings.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A dead bot is refused without a teleport unless `AiPlayerbot.OwnedBotSummonRevive` also resurrects it first.

</details>

### `AiPlayerbot.OwnedBotSummonRevive`
Automatically revives a dead bot when it is summoned to the leader upon group invitation acceptance. Useful for quick recovery.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `1`, a dead bot is revived before it is teleported to the group leader.

Requires `AiPlayerbot.OwnedBotSummonAllowBotDead = 1` to be effective.

</details>

### `AiPlayerbot.OwnedBotSummonRepair`
Automatically repairs bot equipment durability upon arrival from a group accept summon. Prevents broken gear after wipes.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, all equipped gear on the bot is restored to 100% durability upon arrival.

Repairs occur out of combat unless `AiPlayerbot.OwnedBotSummonAllowInCombat` is also enabled.

</details>

### `AiPlayerbot.OwnedBotSummonCooldown`
Specifies the minimum cooldown in seconds between group accept summons for each bot. Prevents exploit loops.
**Default:** `300` · **Values:** seconds (`0..3600`; `0` disables cooldown) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Enforces a 5-minute delay between group accept teleports per bot.

Repeated uninvite and invite cycles within this window refuse the automatic teleport. This ensures the summon mechanism cannot be abused to gain free in-combat resurrections or repairs.

</details>

### `AiPlayerbot.RollBadItemsWithPlayer`
Rolls Need on filler-classed (BAD_EQUIP) equipment anywhere.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, bots roll Need on equipment the item scoring classes as filler for their slot, anywhere it drops.

Genuine upgrades are Needed regardless of this flag. Pool bots and alts wait for humans to roll and step down to Greed if any human rolls Need; hires with an active master roll at once, and autonomous alt-bots without a master only Greed or Pass.

</details>

### `AiPlayerbot.AltBots`
Allows owned alts brought into the world to continue playing autonomously when removed from the player party. Enable for a personalized world.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled (`1`), an owned alt brought in with `.bot add` that leaves the owner party continues playing in the world as an autonomous alt-bot.

It adopts random pool leisure and progression strategies, occupying one pool bot slot. It remains active until dismissed with `.bot remove`, re-invited, or logged into by the player. When disabled (`0`), a kicked alt remains an idle companion.

</details>

### `AiPlayerbot.AltBotsLogoutWithOwner`
Controls whether active autonomous alt-bots log out when their account owner disconnects from the realm.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When alt-bots are enabled, setting this to `1` causes all active alt-bots belonging to a player to log out automatically when that player logs out.

When set to `0`, alt-bots remain active in the world until manually removed or until the server restarts. Alt-bots do not persist across server restarts.

</details>

### `AiPlayerbot.SummonAtInnkeepersEnabled`
Enables summoning owned companion bots while the player master is near an innkeeper NPC.
**Default:** `1` (built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Allows players to summon their owned companion bots when standing within sight distance (`AiPlayerbot.SightDistance`, 60 yd default) of any innkeeper NPC, in addition to standard meeting stone summon locations. The bot (or player) must carry a Hearthstone and its hearthstone cooldown must be ready.

Set to `0` if you want companion bot summoning restricted to meeting stones in range and explicit summon commands.

</details>

### `AiPlayerbot.SyncQuestForPlayer`
Completes quests for the real-player master when a companion bot turns them in.
**Default:** `0` (built in; the key is not in the template — add it to `aiplayerbot.conf` to change it) · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, whenever a bot interacts with a quest giver and completes a quest, that quest is automatically completed for the real-player master as well if the master is on the quest and eligible.

When disabled (`0`), quests turned in by bots do not automatically complete for the master.

</details>

### `AiPlayerbot.SyncQuestWithPlayer`
Completes quests for companion bots when the player master turns them in.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When enabled, bots automatically complete quests the moment the player master hands them in, and bots ignore looting quest items.

When disabled (`0`), bots track and collect quest objectives and hand in quests independently.

</details>

### `AiPlayerbot.BotCheats`
Convenience and survival cheats active on player-owned companion bots and alts.
**Default:** `repair,breath` · **Values:** comma-separated cheat flags · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Configures the cheat mask applied to player-owned bots and alts. Available cheat keywords include:

- `repair`: No durability loss, including resurrection sickness.
- `breath`: Grants underwater breathing to prevent drowning while navigating water.
- `item`: Grants unlimited conjured sustenance items and spell reagents.
- `gold`: Free repairs and vendor purchases.
- `health`: Rapid health regeneration.
- `mana` / `power`: Rapid mana/resource regeneration.
- `cooldown`: Eliminates ability cooldowns.
- `movespeed`: Greatly increases movement speed.
- `attackspeed`: Greatly increases attack speed.
- `taxi`: Unlocks all flight paths.
- `quest`: Quests complete immediately.

Masterless random pool bots use [`AiPlayerbot.RndBotCheats`](world-population.md#aiplayerbotrndbotcheats) instead. Hired companions are also random-flagged, so they read `RndBotCheats` except for maintenance, autogear, and group-accept summoning, which accept active hires. Autonomous alt-bots also get the `item` cheat.

</details>
