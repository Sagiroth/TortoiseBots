---
id: guide-player-controls
title: Available Bot Commands & Addon Controls
category: guides
summary: Reference of the `.bot` command surface, tactical actions, and addon transport protocols.
tags: [guide, commands, player-controls, addon, tactics, cli]
relates_to:
  - guide-getting-started
  - class-overview
  - concept-strategy-engine
---

# Available Bot Commands & Addon Controls

TortoiseBots provides a native, intent-driven command suite (`.bot`) that powers both the **[TortoiseBotsManager](https://github.com/Sagiroth/TortoiseBotsManager)** (`/tbm`) addon and standard in-game chat commands.

Bot-targeted commands require the requesting player to own the target bot (same account) or be a GameMaster. `.bot role self` changes only the requesting player's group-role override and does not target a bot.

### Who may use what (issue #294)

Whisper commands (`/w <BotName> <command>`) and `.bot command <BotName> <command>` pass one gate in `PlayerbotAI::HandleCommand`, keyed by the command's first word. GM means the sender's session security is above `SEC_PLAYER`; owner means the sender's account matches the bot's durable owner account (`BotManager` ownership record). Random bots grouped with a player treat that player as a group member, not an owner. Unknown commands default to owner-or-GM (fail closed).

| Tier | Who | Commands |
| :--- | :--- | :--- |
| GM only | GameMasters (owner NOT included) | `cheat`, `debug` (incl. remote `debug <…>` diagnostics), `cdebug`, `cs`, `log`, `set value`, `teleport`, `load ai`, `save ai`, `list ai`, `reset ai` / `reset strats` / `reset values` |
| Owner or GM | Same-account owner, or any GM | `sendmail`/`mail`, `bank`/`gb`, `ah`/`ah bid`/`ah cancel`, `t`/`trade`/`nt`, `s`/`sell`, `b`/`buy`/`bb`, `repair`, `destroy`, `drop`, `e`/`equip`, `ue`/`unequip`, `keep`, `u`/`use`, `craft`, `guild` + guild shorthand (`gi`/`gk`/`gl`/`gp`/…), `invite`/`join`/`lfg`/`leave`, `summon`, `taxi`, `co`/`nc`/`de`/`react`/`all` strategy changes, `talents`, `reset`, `release`/`revive`/`corpse run`, `trainer`, `skill`, `faction`, `outfit`, `go`, `range`, `flag`, `speak`, `cast`/`castnc`/`spell`, `pet`, `buff`, `share`, `accept`, `talk`, `q`/`quests`, `rep`, `roll`, `ll`, `ss`, `chat`, `home`, `logout`, `do`/`d` item-use shortcuts, `doquest`, `grind`, `r`/`reward`, `bg free`, `move style`, `give leader`, `focus`/`boost`/`follow`/`revive` target assignments, `ra` |
| Group member (tactical/info) | Anyone currently grouped with the bot | `follow`, `stay`, `guard`, `free`, `wander`, `flee`, `runaway`, `attack`, `pull`, `tank attack`, `rti`, `formation`, `stance`, `save mana`, `max dps`, `possible attack targets`, `attackers`, `position`, `who`, `where`, `wts`, `stats`, `c`/`items`/`inv` count, `spells` list, `hire` (self-gated to random bots), `emote`, `help`, `warning`, `ready`, `rebuff`, `queue`, `los`, `wait`, `jump` |

Refused commands answer with a short reason in chat; addon-originated requests additionally receive exactly one `TBM:ACTION_ERR|<command>|denied|<reason>` line so the `/tbm` UI can surface it.

**Pool-bot trading (issue #469):** a masterless pool bot refuses a real-player stranger's trade window (`AiPlayerbot.PoolBotTradeMode = 0`, the default) and ignores Trade-channel chatter unless the speaker addresses it directly. Owned and hired bots always trade with their master; `1` limits the pool to master/group/hire-master trades, `2`/`3` allow buy-only/sell-only, `4` restores unrestricted trading. Bot-to-bot trade life (RPG giveaways, enchants, `WTS`/`WTB`) is untouched.

---

## 1. Tactical Party Actions (`.bot action <intent>`)

Hazard-avoiding chase movement keeps an idle movement generator while its detour spline runs, so the next movement update has a valid default.


The modern control plane operates on **player intent**. Instead of micromanaging each bot, you issue high-level tactical commands; the server resolves target scope and selects appropriate bot executors:

| Command | Target Required | What It Does |
| :--- | :--- | :--- |
| `.bot action attack` | Hostile Target | Controllable party DPS/tank bots engage your current target immediately (dedicated healers keep healing). A creature in evade mode — one the core has sent back to its spawn point, absorbing every point of damage aimed at it — is refused with "is evading": nothing can hurt it until it stops moving. |
| `.bot action interrupt` | Casting Hostile | Evaluates party bots and orders the first party bot with a ready interrupt (e.g. *Kick*, *Pummel*, *Earth Shock*, *Counterspell*); if it is out of range the bot first closes distance and then casts. |
| `.bot action stop` | None | Clears combat queues and stops current attacks. |
| `.bot action flee` | None | Break off: bots drop combat (and any pull hold) and follow you in passive mode without attacking. The next tactical order — `attack`, `pull`, `pullback`, `focus skull`, `follow`, `stay` — ends the passive hold; a pull brings the whole party back. |
| `.bot action pull [seconds]` | Hostile Target | Directs the party tank to pull your target with ranged attack/taunt and stay fighting there; the tank keeps the pulled mob as its target and runs its normal rotation without a second order. The tank stands still until the shot has actually landed (the ranged shot is an auto-repeat — running or casting during the windup cancels it), then moves on. Other bots hold at your position for `seconds` (0-60, default 10) after the pull lands, then join. Healers keep healing throughout. Released early by `.bot action attack` / `stop`. A body-pull (tank walks in, no ranged option) is reported in the ACK. A ranged pull walks to a firing position with a point move and shoots from there (the core's chase cannot carry a puller to shooting range inside a dungeon); the point is only used when it is reachable, on the tank's own floor and in sight of the mob, otherwise the tank advances towards the mob until it can shoot, and one already in shooting range with line of sight does not reposition. If the move cannot start at all, the command reports `unreachable` instead of acknowledging. A pull that draws no aggro (blocked shot, evade, 15 s without an arrival) ends as a failed pull: the tank drops the target, leaves combat and follows you again, and the waiting bots are released at once. |
| `.bot action pullback [seconds]` | Hostile Target | Tank approaches only as close as needed, pulls (from range when it has a working ranged option — standing still until the shot lands, otherwise walking in and hitting the mob in melee once, reported as `body-pull` in the ACK), then returns to your position, holds there and picks the mob up when it arrives. All other bots stand at that spot and attack `seconds` (0-60, default 5) after the tank is back. The tank holds the anchor only for that fight — once it is over (mob dead, no live target, out of combat) it resumes following you. Released early by `.bot action attack` / `stop`. A pullback that draws no aggro parks nobody: no anchor hold is placed, the tank follows you again and the waiting bots are released at once. |
| `.bot action come` | None | All bots sprint directly to the player's exact coordinates. |
| `.bot action stay` | None | Bots halt at their current position and hold ground. `hold`/`comestay` are aliases of *come* and move every bot to your current coordinates. |
| `.bot action follow` | None | Bots break current movement and resume tight follow formation behind the leader. |
| `.bot action focus skull` | Enemy / None | Sets or targets the **Skull** raid icon; orders all party DPS bots to focus fire on that target. |
| `.bot action cc <mark> [bot]` | Owned Bot / Marked Mob | Assigns the mark to one bot (**exclusive ownership**: any other owned party bot holding the mark is reset to `none`). An explicit bot name assigns that bot without juggling targets; otherwise a targeted owned bot is assigned, else a capable bot is selected server-side (Mage *Polymorph*, Rogue *Sap*, Warlock *Banish*/*Fear*, Priest *Shackle Undead*, Druid *Hibernate*/*Entangling Roots*, Hunter *Freezing Trap*/*Scare Beast*, Paladin *Turn Undead*). Unknown names fail with `no-bot`. Inside non-raid dungeons the bot CCs only its assigned mark; in the open world it may CC a free pick. Never CCs over an existing CC. |
| `.bot action cc clear [bot]` | Owned Bot / None | Dismisses CC ownership: an explicit name clears only that bot; otherwise a targeted owned bot is cleared, else all owned live party bots. The roster snapshot drops cleared owners. |
| `.bot action auto cc [on\|off]` | None | Opt-in smart auto CC (**OFF by default**): a CC-capable bot on its own sheeps a loose add that is hitting a party healer/caster (never the tank), that nobody in the group is attacking (members + pets), that carries no periodic-damage aura, and that is not the last enemy, skull-marked, or the tank's target. One bot per mob and one auto target per bot; a broken sheep is never re-sheeped once DoT'd/attacked. Explicit `.bot action cc` marks always win; the toggle bypasses the 5-man dungeon mark gate while ON, and while ON this loose add is the only target the bot picks on its own (no legacy free CC anywhere). The stage-1 AoE interlock protects the auto-sheeped mob. Persisted per bot like the loot toggle. |
| `.bot action aoe <on\|off>` | None | Toggles whether DPS bots cast high-damage AoE abilities. AoE triggers additionally refuse to fire while a deliberately CC'd mob (Polymorph, Sap, Gouge, Shackle Undead, Hibernate, Freezing Trap, Seduction, Repentance, Wyvern Sting) is in or beside the pack, so a stray sheep is not broken even with AoE left on. Frost Nova does not block AoE. |
| `.bot action loot [on\|off]` | None | Toggles corpse looting across scoped bots. |
| `.bot action repair` | None | Orders scoped bots to repair gear at a nearby vendor. |
| `.bot action sell` | None | Orders scoped bots to sell grey vendor trash. |
| `.bot action rest` *(or `drink`, `eat`)* | None | Orders scoped bots to sit and consume food/drink until full. |
| `.bot action release` | None | Commands dead companion bots to release spirit. |
| `.bot action corpse run` | None | Commands spirit bots to run back to corpse. |
| `.bot action learn` | None | Commands scoped bots to learn spells from nearby trainers. |
| `.bot action trade` | Companion Bot Target | Opens trade with the targeted companion bot. |
| `.bot action ready` | None | Initiates a group ready check across all party bots. |
| `.bot action raid status` | None | Reports per-bot raid strategy state (`molten core`, `onyxia`, `four horseman`, `solnius`, … or `outdoor`). |
| `.bot action raid tankface` | Dragon Target | Orders tank bots to drag the current dragon boss away from the raid anchor so breath/cleave miss the raid. |
| `.bot action raid douse` | None | Orders scoped bots to douse nearby MC runes (Eternal Quintessence first, Aqual fallback). |
| `.bot action raid custom status` | None | Reports per-bot custom Turtle raid state (`emerald sanctum`, `lower karazhan`, `karazhan crypt`, or `off`). |
| `.bot action raid custom on` | None | Enables custom Turtle raid transition strategies on scoped bots (gated by `AiPlayerbot.EnableCustomRaidTactics`). |
| `.bot action raid custom off` | None | Disables custom Turtle raid transition strategies on scoped bots for manual control. |

---

## 2. Roster & Lifecycle Commands

These commands manage the login, party membership, and presence of your owned bots:

Inside dungeons and raids, autonomous grind/attack-anything selection does not
start fresh pulls while a bot shares a group with its connected human master.
It can defend enemies already fighting group members or their pets. Explicit
attack targets and configured attack raid marks remain allowed; manual
attack/pull commands, open-world grinding and solo bots are unchanged.

| Command | Syntax | What It Does |
| :--- | :--- | :--- |
| **Roster Snapshot** | `.bot roster` | Returns an authoritative snapshot of all owned characters on your account and their online/party state (emits structured `TBM:ROSTER`). Hired companions (random-pool characters) are not listed: a hire's character is deleted when the hire ends. |
| **Login Bot** | `.bot add <Name>` | Logs in an owned character from your account as a headless bot. |
| **Hire Companion** | `.bot hire <class> [role] [race] [gender]` | Recruits a fresh companion at your level from an inn: creates a new character, provisions talents, spells, skills, and spec-weighted gear, then invites it to your party. Once grouped, the companion posts its spec and key spells in party chat (once per hire). Hiring happens at a `<Mercenary Hire>` recruiter; the command works anywhere only when the server sets `AiPlayerbot.HireAnywhere = 1`. Costs level-scaled gold (party hires escalate, raid hires are flat). Druid spec words `cat`/`balance` hire Cat/Balance DPS, `bear`/`feral` hire a Bear tank. The recruiter's spec step is honoured for every class (Arms vs Fury, Frost vs Fire, …): the chosen premade tree is applied and checked after provisioning, falling back to the role only when the class has no premade build for that spec. **A companion is temporary**: it exists while you are online (plus `AiPlayerbot.HireDisconnectGracePeriod` seconds after you disconnect) and its character is deleted when the hire ends (leaving the party dismisses your companions at once; logging out keeps the disconnect grace period) — every hire is a new character and costs a new fee. |
| **Set Role** | `.bot role <Name> <tank|healer|dps|clear>`<br>`.bot role self <tank|healer|dps|clear>` | Designates an owned bot's party role and rebuilds its strategy kit; `self` sets the requesting player's runtime role override for bot role detection, and `clear` restores automatic inference. The requesting player's name is also accepted in place of `self`. |
| **Logout Bot** | `.bot remove <Name>` *(or `logout`)* | Cleanly logs out an active headless bot on your account. On **your own** bot, durable ownership is retained as before; on a **hired companion** the hire ends and its character is deleted. |
| **Invite to Party** | `.bot invite <Name>` | Sends a party invite to an online bot on your account. |
| **Uninvite from Party**| `.bot uninvite <Name>` *(or `kick`)*| Removes an owned bot from your group. Removing a hired companion from the group ends the hire and deletes its character, with no grace period. |
| **Summon** | `.bot summon <Name>` | Teleports one owned bot safely to your location out of combat. Works for your own bots and hired companions; no config switch needed. |
| **Maintenance** | `.bot maintenance <Name>` | Refreshes one owned bot's kit (consumables, reagents, ammo, repair). Opt-in alternative play style for owned bots and hired companions (never pool bots): needs `AiPlayerbot.OwnedBotMaintenanceEnabled = 1`, otherwise the command replies that the feature is disabled. |
| **Autogear** | `.bot autogear <Name> [quality\|ilvl]` | Re-gears one owned bot to its level within the quality/ilvl caps (default green, optional e.g. `blue` or `200`). Opt-in like maintenance (`AiPlayerbot.OwnedBotAutogearEnabled = 1`, caps `OwnedBotAutogearQualityCap`/`OwnedBotAutogearIlvlCap`); pool bots keep progressing organically. |
| **Summon on group accept** | automatic | When `AiPlayerbot.OwnedBotSummonWhenGroup = 1`, an owned bot (or hired companion) that accepts your group invite is summoned to you if it is far away (beyond sight distance, same map). Condition knobs: `OwnedBotSummonAllowInCombat`, `OwnedBotSummonAllowMasterDead`, `OwnedBotSummonAllowBotDead` (+ `OwnedBotSummonRevive`), `OwnedBotSummonRepair`. Revive/repair run only out of combat unless `AllowInCombat` is also on; a per-bot cooldown (`OwnedBotSummonCooldown`, 300 s) bounds uninvite/invite abuse. All off by default except the cooldown. |
| **Release Spirit** | `.bot release` | Commands dead companion bots to release spirit to the graveyard. Also available as `.bot action release`. |
| **Corpse Run** | `.bot corpse run` | Commands spirit bots to run back to their corpse or instance entrance. Also available as `.bot action corpse run`. |
| **Learn Spells** | `.bot learn` | Commands companion bots near matching trainers to learn affordable spells. Also available as `.bot action learn`. |
| **Trade** | `.bot trade` | Opens a trade window with the targeted alive companion bot. Also available as `.bot action trade`. |

---

## 3. Direct Party & Movement Commands

Direct command shortcuts that operate on your targeted bot or all party bots:

| Command | Parameters | What It Does |
| :--- | :--- | :--- |
| `.bot follow` | `<Name>` | Orders one owned bot to follow the requester (party-wide: `.bot action follow`). |
| `.bot stay` | `<Name>` | Orders one owned bot to stay at its current location (party-wide: `.bot action stay`). |
| `.bot guard` | `<Name>` | Orders one owned bot to guard its current position and engage nearby threats. |
| `.bot free` | `<Name>` | Releases one owned bot from stay/guard back to free autonomous movement. |
| `.bot ready` | None | Checks if party bots are ready (health/mana full, buffs active). |
| `.bot rebuff` | None | Tops off expiring party buffs out of combat (2-min pass; heals yield to buffs while it runs). |
| `.bot attack` | `<Name>` | Orders one owned bot to attack your current hostile target (party-wide: `.bot action attack`). |
| `.bot interrupt` | `<Name>` | Orders one owned bot to interrupt your current target's cast (party-wide: `.bot action interrupt`). |
| `.bot pullback` | `[Name]` | Dispatches pullback maneuver on the specified bot or designated tank. |
| `end pull` (whisper) | None | Aborts a pull that already started: the tank drops the pull target and goes back to following you instead of waiting out the 15-second timeout. Does nothing when no pull is running. Whisper it to the pulling tank when a pull evades or gets wedged. |
| `.bot strategy` | `<+|-|~strategy> [Name]` | Applies a strategy change to the selected bot, named bot, or all owned party bots (e.g. `.bot strategy +loot`, `.bot strategy -passive`). Also available per-bot via whispers (`co`, `nc`) or `.bot command <BotName> <change>`. |
| Single-target burn toggle | `co +focus` / `co -focus` | Off by default. While on, the bot vetoes area spells (heals still work) and ranged debuffs aimed at arbitrary attackers, so nothing splashes onto crowd-controlled or marked packs. Melee abilities are vetoed when their spell data targets an enemy area (whirlwind, cleave); traps, totems and other non-cast actions are not covered — keep those bots out of CC range regardless. Coverage intentionally exceeds the original here (consecration, shouts, frost nova and fears also stop), since breaking less CC is the point of the toggle. Use it on pulls with sheep, sap or shackle out. |
| AoE fear toggle | `co +aoe fear` / `co -aoe fear` | Priest Psychic Scream, warlock Howl of Terror and warrior Intimidating Shout are off by default inside dungeons/raids and while the bot is grouped with a player, because a feared mob pulls the next pack. `co +aoe fear` allows them again for that bot. To block a single spell anywhere, use the skip-spell list (`ss`). |
| `.bot formation` | `<arrow\|queue\|near\|line\|circle\|shield>` | Sets the geometric follow formation around the party leader. |
| `.bot loot` | `[on\|off]` | Toggles corpse looting on the targeted bot (or all party bots); on by default. Also available as `.bot action loot [on\|off]`. |
| `.bot repair` | None | Orders the targeted bot (or all party bots) to repair gear at a nearby vendor. Also available as `.bot action repair`. |
| `.bot sell` | None | Orders the targeted bot (or all party bots) to sell grey vendor trash. Also available as `.bot action sell`. |
| `.bot rest` *(aliases `drink`, `eat`)* | None | Orders scoped bots to sit and consume food/drink until full health/mana (combat or master movement breaks rest). Also available as `.bot action rest`. |

---

## 4. Diagnostics & Inspection Commands

Commands for checking bot state, lifecycle, and fleet metrics:

| Command | Parameters | What It Does |
| :--- | :--- | :--- |
| `.bot list` | None | Lists all online bots currently owned by your account (Name, lifecycle, random flag, AI status). |
| `.bot status` | `<Name>` | Displays detailed status of an owned bot: lifecycle (`starting`, `in world`, `removing`), AI attachment, active movement strategy (`follow`, `stay`, `guard`, `free`, `custom`), and owner. |
| `.bot stats` | None | Summarizes owned bot fleet: total online, random bots, and bots with active AI attached. |
| `.bot lease` | `[status]` | Reports autonomous activity lease counts (Idle, Grinding, Trading, LftQueued, BgQueued, PlayerMaster) and lists active lease timers. |
| `.bot version` *(aliases `v`, `about`, `credits`)* | None | Prints the server build version (`TortoiseBots <UTC date>-v<N>`), then `TBM:VERSION|<version>` for the addon, then the author credit and source link (`by Sagiroth - https://github.com/Sagiroth/TortoiseBots`); any player may use it. The same credit line is sent to every player on login and written to the startup log. |
| `.bot help` *(alias `h`)* | None | Prints the server version (`TBM:VERSION|<version>`). |

---

## 4a. Bot Panel Commands (gear, bags, behaviour)

Structured commands behind the TBM bot panel. They work on any online bot you control — owned alts and hired companions alike — and answer with `TBM:` lines only.

| Command | What It Does |
| :--- | :--- |
| `.bot inv <Name>` *(alias `inventory`)* | Inventory snapshot: `TBM:INV_BEGIN\|<bot>\|<copper>`, one `TBM:INV_EQ\|<slot>\|<itemId>\|<durability>\|<max>` per equipped item, one `TBM:INV_ITEM\|<bag>\|<slot>\|<itemId>\|<count>\|<flags>` per bag item, then `TBM:INV_END\|<bot>\|<free>\|<total>`. Flags: `e` the bot can equip it, `u` the item-usage value rates it an upgrade, `b` it cannot be traded to you right now (soulbound/quest), `-` none. |
| `.bot item <Name> trade` | The bot opens a trade window with you. Put items or gold in and accept; the bot accepts on its own. |
| `.bot item <Name> equip <bag> <slot>` | The bot equips that exact bag item (bag/slot as reported by `.bot inv`). Answers an ACK/ERR and a fresh snapshot. |
| `.bot item <Name> unequip <bag> <slot>` | The bot moves that equipped item (bag `255`, slot `0`-`18`) into its bags. Fails with `failed` when the bags are full. |
| `.bot item <Name> give <bag> <slot>` | Puts that bag item into a trade window with you. Without an open trade the bot opens one and answers `pending`; repeat the command once the window shows (the addon does this automatically). Soulbound/quest items are refused up front (`no-trade`) because the core would cancel the whole trade. |
| `.bot behavior <Name> <key> <on\|off>` *(alias `behaviour`)* | Per-bot behaviour toggle, persisted like the loot toggle. Keys: `loot` (loot corpses), `aoe` (`dps aoe`), `autocc` (`auto cc`), `savemana` (`conserve mana`), `boost` (offensive cooldowns), `threat` (ease off near tank threat), `potions` (reaction engine), `spread` (opt-in combat spacing, off by default), `mimic` (`mimic consumables`, off by default: the bot drinks the class/spec/level-appropriate equivalent when you use an elixir, flask, scroll, weapon imbue, protection potion, stat food, alcohol, raid buff or utility potion out of combat — capped by your item's level band so a cheap lowbie drink never grants BiS). Use `.bot behavior party\|raid mimic on\|off` for the whole group. Small groups (up to 4 bots) whisper what they use; larger raids stay silent and you get one `[Raid Mimic]` summary line. Answers an ACK and the bot's `TBM:BOTSTATE` line. |

Replies use the action protocol: `TBM:ACTION_ACK|item <op>|bot:<Name>|1|<itemId>[ pending]`, `TBM:ACTION_ACK|behavior <key>|bot:<Name>|1|<on|off>`, or `TBM:ACTION_ERR|<intent>|<code>|<message>`.

---

## 5. Mature AI Command Delegation & Whispers

In addition to high-level `.bot action` party commands, you can delegate commands directly to a specific bot either via chat whispers (`/w <BotName> <command>`) or through the `.bot command` CLI proxy:

```text
.bot command <BotName> <command>
# Example: .bot command Dudette trainer
```

### Comprehensive Whisper Command Cheat-Sheet

| Category | Command / Whisper | What It Does |
| :--- | :--- | :--- |
| **Inventory & Bags** | `c` / `items` / `inv` | Lists equipped and carried items with counts in chat. |
| | `e <item>` / `equip <item>` | Equips the specified item link or item name from bags. |
| | `ue <slot>` | Unequips gear from the specified equipment slot into bags. |
| | `u <item>` | Uses an item from inventory (potions, quest items, bandages). |
| | `destroy <item>` | Destroys an item from inventory to free up bag space. (`drop <quest>` abandons a quest.) |
| **Vendors & Repairs** | `s [gray\|all\|<item>]` | Sells gray junk items or specific items when targeted vendor is open. |
| | `b <item>` | Purchases the specified item from the open merchant window. |
| | `bb <item>` | Buys back an accidentally sold item from the vendor. |
| | `repair` | Repairs damaged equipment at a blacksmith / armorer NPC. |
| | `t` | Initiates or accepts a direct trade window with the player. |
| **Quests & NPCs** | `q` / `quests` | Prints active quests and current objective completion status. |
| | `accept` | Accepts an offered quest from a nearby quest giver. |
| | `talk` | Interacts / gossips with the current NPC target. |
| | `r [choice]` | Selects the quest reward and turns in a completed quest. |
| | `share` | Shares eligible quests from the bot's quest log with party members. |
| **Training & Skills** | `trainer` | Automatically learns all currently available spells and ranks from a class trainer! |
| | `talents` | Prints spent talent points and tree distribution. |
| | `talents shift <link>` | Counts currently allocated ranks before spending remaining talent points toward the requested build. |
| | `spells` | Lists known spells and highest learned spell ranks. |
| **Travel & Life** | `home` | Interacts with an innkeeper to bind the bot's Hearthstone. |
| | `taxi <destination>` | Purchases a flight path to the specified flight master destination. |
| | `release` | Releases spirit after dying. |
| | `corpse run` | Paths as a ghost back to the corpse location and resurrects. |
| | `revive` | Resurrects immediately at the Spirit Healer (with resurrection sickness). |
| **Combat Overrides** | `flee` / `runaway` | Drops combat anchors and retreats toward the master/tank. |
| | `tank attack` | Forces the party tank to prioritize and taunt your target. |
| | `pet` | Commands pet behavior (`pet follow`, `pet stay`, `pet attack`). |
| | `buff` | Prompts the bot to re-cast missing class buffs on party members. |
| | `grind` | Toggles solo autonomous grinding mode for the bot. |
| | `reset` | Flushes AI action queues and resets combat strategy states. |

---

## 6. Auction House Management (`.bot ah` / `.ahbot`)

*(GameMaster / Administrator only)*

Manage and monitor the autonomous synthetic Auction House engine in real time:

| Command | Parameters | What It Does |
| :--- | :--- | :--- |
| `.bot ah status` | None | Displays synthetic AH engine status, inventory size, active listings, and telemetry. |
| `.bot ah reload` | None | Reloads price overrides and blacklist filters from the `ahbot_items` DB table. |
| `.bot ah rebuild` | `[all]` | Triggers an immediate market evaluation pass. Passing `all` expires active unbid synthetic listings to refresh the market. |
| `.bot ah item` | `<id>` | Checks current blacklist status or active price overrides for an item ID. |
| `.bot ah item` | `<id> reset` | Removes any custom price override for an item ID, restoring formula pricing. |
| `.bot ah item` | `<id> <value> [chance] [min] [max]` | Sets custom price (copper), posting chance (%), and stack bounds. Passing `0 0` **blacklists** the item from being posted. |

---

## 7. Managed Pool Administration (`.bot pool`)

*(Administrator / server console)*

Inspect the managed random-bot pool and enroll legacy accounts. See
[Resetting the managed bot pool](living-world.md#resetting-the-managed-bot-pool)
for the full rebuild procedure.

| Command | Available to | What It Does |
| :--- | :--- | :--- |
| `.bot pool status` | In-game administrators and the server console | Shows the configured reset mode and requested generation, the applied generation, managed account/character counts, the current reset phase and progress, and the last failure reason. |
| `.bot pool accounts` | **Server console only** | Lists every managed pool account with its registration source and current character count. |
| `.bot pool adopt preview` | **Server console only** | Read-only. Lists every account matching `AiPlayerbot.RandomBotAccountPrefix`, whether it is already managed, and its character count, then prints a short-lived confirmation challenge. |
| `.bot pool adopt confirm <challenge>` | **Server console only** | Enrolls the previewed accounts. Requires an unexpired challenge and an unchanged account set; re-running it is idempotent. **Registers accounts only — it never deletes, moves, or edits a character.** |

Notes:

- Adoption is the only way a pre-existing hand-made bot account becomes managed. Startup logs a warning listing prefix-matching accounts that are not managed, and those accounts are ignored by the pool until adopted.
- The console-only commands require a session-less handler **that carries `SEC_CONSOLE`**. Players, addon messages, hired bots, and headless sessions all have a session and are rejected; remote-access and Discord commands use session-less handlers too, but carry the calling account's own security level, so they are rejected as well. Every `.bot pool` handler enforces this itself, because the module intercepts `.bot` before the core command table can apply its own permission checks.
- There is deliberately **no** live reset command: rebuilding the pool is a startup operation driven by `AiPlayerbot.RandomBotPoolReset`.

---
## 7a. World Buffs at Capital Recruiters (issue #492)

The six capital `<Mercenary Hire>` recruiters (Stormwind, Ironforge, Darnassus, Orgrimmar, Undercity, Thunder Bluff) offer a `World buffs` branch next to `Hire bots` for level 60+ players. Every other recruiter opens the hire wizard directly. Pool bots only ever see the hire wizard.

Each of the seven buffs unlocks once per character with a quest from the same broker (turn-in fee 200g for the three boss buffs, 100g for the rest; no XP, no reward, non-repeatable). Until unlocked, the branch shows the quest; after the turn-in it shows the purchase instead — one row per buff, never both.

| Buff | Unlock (once) | Purchase (each use) |
| :--- | :--- | :--- |
| Rallying Cry of the Dragonslayer (2 h) | Slay Onyxia or Nefarian | 10g + 2g/person |
| Warchief's Blessing (1 h) | Slay Rend Blackhand | 10g + 2g/person |
| Spirit of Zandalar (2 h) | Slay Hakkar | 10g + 2g/person |
| Dire Maul Tribute (2 h, all three) | Earn any one guard's favor in Dire Maul | 10g + 2g/person |
| Sayge's Dark Fortune (2 h, one of 8) | Receive any fortune at the Darkmoon Faire | 6g + 1g/person |
| Songflower Serenade (1 h) | Stand in a cleansed songflower's blessing in Felwood | 4g + 1g/person |
| Traces of Silithyst (30 min) | Defeat 5 opposite-faction players in Silithus | 2g + 40s/person |

A purchase buffs you plus grouped members standing within 40 yd of the recruiter (price = base + per-person x head count, so a solo buy costs base + one share). The aura is applied directly with its original duration and death rules; random bystanders get nothing. Sayge purchases pick one of 8 fortunes from a submenu and replace any other active fortune. The Dire Maul purchase lands all three guard buffs at once.

## 8. Addon Command Transport (`TortoiseBotsManager`)

The `/tbm` UI speaks the same command grammar over a silent transport. While the
server reports the addon channel usable, the addon sends its UI commands as
addon messages instead of `.bot` chat, and the module answers over the same
channel: the request is consumed by the server, no echo reaches nearby players,
and no reply line lands in the chat frame. `.bot` chat stays the transport when
the addon channel is unavailable — ungrouped, a battleground group with no
pre-battleground group, or an older module — and for every hand-typed command.

- Request — addon message prefix `TBM`, body `<verb> [args]` (`action attack`).
- Reply — addon message prefix `TBM`, one line per reply, `TBM:` protocol lines included.
- Verdict — `TBM:TRANSPORT|party` or `TBM:TRANSPORT|none`, trailing each roster response.
- Bot state — `TBM:BOTSTATE_BEGIN|<n>`, one `TBM:BOTSTATE|<bot>|move=<follow|stay|guard|free|flee|custom>,loot=on,aoe=off,autocc=off,savemana=off,boost=on,threat=on,potions=on,mimic=off`
  per controllable party bot, then `TBM:BOTSTATE_END`, trailing each roster response. It drives
  the addon's party switches, the Party tab movement label and the bot panel's behaviour tab.
- Capabilities — `TBM:CAPS|pull-seconds,flee,inventory,behavior,claimed-roster`: adjustable pull delays,
  `.bot action flee`, `.bot inv`/`.bot item`, `.bot behavior` with the BOTSTATE stream, and the
  `TBM:CLAIMED_BEGIN|<n>` / `TBM:CLAIMED|<guid>|<name>|<class>|<level>|<state>|<grouped>|<location>|<gearLocked>` /
  `TBM:CLAIMED_END` claimed-guild-bot snapshot (issue #489) for the addon's Guild tab.
  The addon hides the matching controls when a capability is missing.
- Version — `TBM:VERSION|<UTC date>-v<N>` trails every roster response, and
  answers `.bot version` and `.bot help` directly. The addon shows it as
  `server <version>` in the `/tbm` window (`server ?` on older servers).

Hand-typed `.bot` commands keep their chat replies, so the CLI and macro surface
is unchanged.

Hazard avoidance preserves accepted waypoint detours in the path sent to movement. If neither side is valid, it does not invent a replacement waypoint.
Talent links accept at most three trees (two `-` separators); malformed extra trees are rejected before parsing.
