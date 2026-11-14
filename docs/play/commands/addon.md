---
id: play-commands-addon
title: TortoiseBotsManager Addon (/tbm)
category: play
summary: Installation, GUI features, and player transport protocol for the TortoiseBotsManager addon.
tags: [addon, tbm, gui, player-controls]
relates_to:
  - play-commands
  - play-getting-started
  - play-commands-bot-panel
---

# TortoiseBotsManager Addon (/tbm)

The **[TortoiseBotsManager](https://github.com/Sagiroth/TortoiseBotsManager)** (`/tbm`) addon provides an integrated graphical interface for managing companion bots in Tortoise WoW. It allows players to spawn owned alts, issue combat commands, inspect gear, and adjust behavior toggles without typing console commands.

## Installation & Setup

1. Download the latest release from the [TortoiseBotsManager GitHub repository](https://github.com/Sagiroth/TortoiseBotsManager).
2. Extract it into your game client's addon folder, so the result is:
   ```text
   World of Warcraft/Interface/AddOns/TortoiseBotsManager/TortoiseBotsManager.toc
   ```
   (not nested inside a duplicate folder).
3. Log into the game and type `/tbm` in chat to toggle the control interface (`/tb`, `/tbot` and `/tortoise` work too).

---

## Addon Panels & Features

The interface is divided into functional tabs designed for different stages of gameplay:

### Roster Tab

- **Character List:** Lists your characters that the server reports as controllable, with class and grouping state. A row shows `online` (live bot with an active session), `starting` (login in progress), or `offline`.
- **Login & Logout:** Log in an owned alt as a headless companion bot, or log it out again.
- **Invite & Summon:** Group with an online owned alt, or summon it to your location.
- **Alt-Bot Indicators:** Highlights alts playing autonomously in the world, allowing you to reclaim them or log them out remotely.

### Party Tab

- **Movement:** Order bots to follow, stay, guard, move freely, come to you, or flee.
- **Combat orders:** Attack, pull, and pullback orders, with adjustable pull delays.
- **Crowd control:** Assign crowd control marks to capable party bots.
- **Toggles:** Party-wide switches for AoE, corpse looting, consumable mimicry, and smart auto crowd control.

### Bot Panel Tab

- **Gear:** View equipped items and durability on your bots.
- **Bags:** Browse carried items, spot upgrades, and equip or unequip them remotely.
- **Behaviors:** Toggle threat backoff, mana saving, offensive cooldowns, and potion use.
- **Totems:** For shaman companions, pin totem choices per element and trigger immediate drops.

### Guild Tab

- Displays claimed guild bots for your account, detailing bot levels and grouping state.
- **Order:** opens the order window for a bot: search an item, choose Farm, Buy or Craft, an amount and a farm time in hours (1-24, or no time limit — the default). See [Guild Orders](../guild-orders.md).

---

## Addon Command Transport (Player View)

The addon talks to the server over private addon messages rather than standard chat channels.

- **Silent Operation:** Addon commands travel as addon messages instead of `.bot` chat text, and server replies return as addon messages; the module consumes its own prefixed payloads so they never reach group chat.
- **Chat Fallback:** If the private addon channel reports `none` (while ungrouped, or in a battleground group with no pre-battleground group), the addon falls back to sending standard `.bot` chat commands.
- **Execution Order & Queueing:** Commands sent via addon-channel messages and typed `.bot` chat share a single per-requester queue drained on the world thread. Because packet delivery and chat dispatch can resolve asynchronously, send order does not guarantee receive order across different channels. Addon commands execute on the server's next world tick, before bots update.
- **Movement Conflict Resolution:** A newer `.bot` movement order drops older chat-queued movement orders (`follow`, `stay`, `guard`, `free`, `flee`, `wander`) queued inside the bot AI; queued `.bot` entries are not cancelled.
- **Exemptions & Teardown:** Action packets the AI handles inline on the world thread (its own casts, hire provisioning) are processed directly and exempt from command queuing. Queued bot chat and packets run in arrival order ahead of each `.bot` command drained on the world tick. Even when the module is globally disabled (`AiPlayerbot.Enabled=0`), the drain still runs and queued orders execute on the world thread before the bot update.
- **Rate Limiting:** The server queues up to 128 bot commands per player; commands beyond the cap are dropped and reported with the message: `Too many bot commands at once: <n> ignored.`

---

## Synchronization Streams

The module transmits structured protocol lines (prefixed with `TBM:`) to keep the addon interface synchronized with server state. Detailed stream definitions and grammar:

| Protocol Stream | Description |
| :--- | :--- |
| `TBM:TRANSPORT\|<channel>` | Reports the addon command channel (`party` or `none`). |
| `TBM:ROSTER_BEGIN\|<n>` / `TBM:ROSTER_END` | Delimits the account character roster stream. |
| `TBM:ROSTER\|<guid>\|<name>\|<class>\|<state>\|<grouped>\|<loc>` | Emits one owned character row (`<loc>` looks like `map:<id>,zone:<id>,area:<id>`, or `-`). |
| `TBM:CLAIMED_BEGIN\|<n>` / `TBM:CLAIMED_END` | Delimits the claimed guild bots snapshot. |
| `TBM:CLAIMED\|<guid>\|<name>\|<class>\|<level>\|<state>\|<grouped>\|<loc>\|<locked>\|<profs>\|<alt>` | Emits one claimed bot row (`<profs>` lists `skillId:value` pairs joined by `;`, or `-`; oldest servers send no field). `<alt>` is `1` for your own alt playing on its own in your guild (it takes guild orders too), `0` for a claimed bot; older servers send no field. |
| `TBM:BOTSTATE_BEGIN\|<n>` / `TBM:BOTSTATE_END` | Delimits live movement and toggle states for controllable party bots. |
| `TBM:BOTSTATE\|<bot>\|<fields>` | Emits bot movement mode, the behavior toggles, the sap switch, the spec strategies and the focus mark. |
| `TBM:CC_ASSIGN_BEGIN\|<n>` / `TBM:CC_ASSIGN_END` | Delimits active crowd control assignments. |
| `TBM:CC_ASSIGN\|<bot>\|<mark>` | Emits a party bot's assigned CC raid mark. |
| `TBM:CAPS\|<capabilities>` | Advertises server feature capabilities. Exact tokens: `pull-seconds,flee,inventory,behavior,claimed-roster,loot-log,downtime,professions,blessing,blessing-player,engage,choice,focus-mark`, plus `guild-orders,guild-order-steps` while the order source index is ready (`AiPlayerbot.AutoDoQuests = 1`), plus `guild-order-nobuy` while player Buy orders are refused (`AiPlayerbot.GuildOrderPlayerBuy = 0`), plus `dungeon-run` while the dungeon run is enabled (`AiPlayerbot.DungeonSelfClear = 1`). |
| `TBM:LOOT_BEGIN\|<n>\|<nextCursor>` / `TBM:LOOT_END\|<nextCursor>` | Delimits one page of loot history from `.bot lootlog`. |
| `TBM:LOOT\|<ts>\|<bot>\|<event>\|<itemId>\|<item>\|<sourceType>\|<source>\|<quality>` | Emits one loot row (item taken or money taken). |
| `TBM:ORDER_BEGIN\|<n>` / `TBM:ORDER_END` | Delimits the guild order list from `.bot order list`. |
| `TBM:ORDER\|<bot>\|<type>\|<itemId>\|<item>\|<have>\|<want>\|<state>\|<reason>\|<zone>\|<difficulty>\|<hours>\|<keep>\|<parent>\|<goldCap>\|<step>\|<need>\|<carry>\|<bring>` | Emits one guild order row; also sent alone as a live update to the giver when state, supply step, missing items or progress changes. `<have>` is what already reached the guild (a bank deposit, the mail hand-over, or the bank stock on a `stocked` row); `<carry>` is the amount in the bot's bags and mailbox that is not delivered yet; it is `0` once the hand-off succeeded (a `done` row after the bank deposit, a `mailed` row), and a `cant` row keeps the last scanned figure because the stack is still in the bags. A list reply reads a live bot's bags while the row runs, and shows the stored figure once the row is over or the bot is offline. `<hours>` is the chosen farm time in whole hours (1-24) or `inf`, `<keep>` is `keep` or `-`, `<parent>` is the parent bot or `-`, `<goldCap>` is guild copper or `-`, `<step>` is `bags`, `bank`, `ah`, `farm`, `craft`, `sub`, `need`, `deliver` or `-`, `<need>` is `itemId:count` pairs separated by semicolons or `-`, and `<bring>` is the ticked base reagents from `bring=` joined by commas or `-`. |
| `TBM:ORDER_ACCESS\|<rankIndex>\|<rankName>\|<tabsOk>\|<tabsDenied>\|<gold 0/1>\|<canSetRank 0/1>` | The guild's bot rank and what it reaches: comma-separated 1-based bank tab numbers (or `-`), whether it may take guild gold, and whether you may change the rank. Sent first inside `ORDER_BEGIN`/`ORDER_END`, after `.bot order rank`, and as the answer to `.bot order access`. |
| `TBM:ORDER_STEPS_BEGIN\|<bot>\|<n>\|<dropped>` / `TBM:ORDER_STEPS_END\|<bot>` | Delimits the on-demand step timeline from `.bot order <bot> steps`: `<n>` entries, `<dropped>` older ones pushed out of the 16-entry ring (memory only, so it resets on restart, a new order or a reassign). |
| `TBM:ORDER_STEP\|<bot>\|<ageSec>\|<state>\|<reason>\|<step>\|<itemId>` | One timeline entry, oldest first: the state word, reason label, step label and leg item the order had at that change; `<ageSec>` is the server's age in seconds (clamped at 0), `<itemId>` the leg item or `0`. Sent only in reply to `.bot order <bot> steps`, never live. |
| `TBM:ORDER_ENCHANTS_BEGIN\|<n>` / `TBM:ORDER_ENCHANTS_END` | Delimits the `.bot order search <bot> enchant <text>` result list (`<n>` the match count, at most 30). |
| `TBM:ORDER_ENCHANT\|<spellId>\|<name>\|<slot>\|<reqSkill>\|<verdict>\|<reagents>` | Emits one enchant the bot knows: `<slot>` the enchant slot (`bracer`, `chest`, `boots`, `cloak`, `gloves`, `shield`, `weapon`, `2hweapon`), `<verdict>` `ok` or `noskill`, `<reagents>` one `itemId:need:have` row per reagent (`have` counts bags plus mail; `;` between rows, `-` when the cast needs none). Sorted by required skill, then name. |
| `TBM:ORDER_ITEMS_BEGIN\|<n>` / `TBM:ORDER_ITEMS_END` | Delimits the `.bot order search` result list. |
| `TBM:ORDER_ITEM\|<itemId>\|<item>\|<quality>\|<sources>\|<minLevel>\|<skill>\|<verdict>\|<zone>\|<chance>\|<difficulty>\|<category>` | Emits one searchable item with the bot's verdict, farm zone and kind; `<category>` is `herb`, `ore`, `leather`, `fish`, `cloth`, `drop` or `other`. `<sources>` lists `drop`, `skin`, `herb`, `ore`, `fish`, `craft`, `ah` (`fish` means the item comes from fishing loot; the bot walks to a fishing spot in the planned zone and fishes until the amount is reached). Usable rows (`ok`) come first across all matches, then name rank, then A-Z; at most 30 rows. |
| `TBM:ORDER_WHO_BEGIN\|<kind>\|<n>` / `TBM:ORDER_WHO_END` | Delimits the guild-wide `.bot order search <craft\|enchant> <text>` result list (`<kind>` is `craft` or `enchant`, `<n>` the match count, at most 30). |
| `TBM:ORDER_WHO\|<kind>\|<id>\|<name>\|<quality>\|<slot>\|<reqSkill>\|<bots>` | Emits one craft or enchant match (craft rows as today; enchant rows carry `<id>` the enchant spellId, `<quality>` always `-`, `<slot>` the enchant slot token, `<reqSkill>` the required Enchanting skill): `<id>` the crafted itemId, `<quality>` its item quality, `<slot>` always `-`, `<reqSkill>` the required profession skill, `<bots>` one `name:skill:have:need:busy` row per online guild bot that knows an orderable recipe for it (`;` between bots, `-` when none; `have`/`need` count distinct reagents the bot holds in full / the recipe needs; `busy` is 1 while the bot runs an order). Bots sort by skill high to low. |
| `TBM:ORDER_PREVIEW\|<bot>\|<type>\|<itemId>\|<item>\|<want>\|<missing>\|<difficulty>\|<zone>\|<reagents>\|<minutes>\|<goldCap>\|<bring>` | Answers `.bot order <bot> preview <farm\|craft> <item> <amount>`: what the bot would do, from the same planner the order uses. `<type>` is `farm` or `craft`; `<missing>` is the output still missing from bags and mail; `<zone>` is the planned farm zone (`-` when none); `<reagents>` lists one `itemId:need:bags:mail:bank:farm:zone:ah:vendor:sub:player` row per base reagent (`;` between rows, `-` when none; `zone` is `-` when none; `player` is what the giver must send); `<minutes>` is the rough worked time; `<goldCap>` is the order's guild gold cap in copper (`-` for no cap); `<bring>` is the ticked base reagents from `bring=` joined by commas or `-`. A refused preview answers with the order `ACTION_ERR` code instead (`buy-disabled`, `norecipe`, ...). |
| `TBM:ALTBOTS\|<names>` | Comma-separated list of owned characters active as autonomous alt-bots. |
| `TBM:SERVICE\|<bot>\|<food>\|<water>\|<healthstone>\|<portals>` | Answers `.bot service <bot>`: 1 when the bot knows any rank of *Conjure Food* / *Conjure Water* / *Create Healthstone*, else 0; `<portals>` lists `spellId:City Name` pairs the mage knows, or `-` when none. |
| `TBM:ALTSUM\|<name>\|<text>` / `TBM:ALTSUM_END\|<count>` | One away-summary line per autonomous alt from `.bot alts` (`<text>` the same plain line shown in chat; `<count>` the alt total). |
| `TBM:VERSION\|<version>` | Handshakes server module version (e.g. `2026-10-20-v1` or `dev`). |
| `TBM:PROTO\|<level>\|<minAddon>` | Addon protocol level this module speaks and the lowest addon level it works with, sent before `TBM:CAPS`. The addon warns once per session (with "Don't show again") and keeps an "outdated" note in its header when either side is too old. The levels change only when a change needs both sides updated; fields appended at the end of a line need no change, the addon ignores them. |
| `TBM:LATEST\|<module>\|<addon>` | Newest TortoiseBots and TortoiseBotsManager releases found on GitHub at server start (`-` when unknown); sent only after a successful lookup (see `AiPlayerbot.UpdateCheck`). |
| `TBM:VIEW` | Real-time perception frame lines for Remote Commander. |
| `TBM:ACTION_ACK\|<intent>\|<scope>\|<count>\|<detail>` | Confirms action execution (`<scope>` is `party`, `raid`, or `bot:<Name>`). |
| `TBM:ACTION_ERR\|<intent>\|<code>\|<message>` | Notifies action failure with structured error code. |
| `TBM:RAID_LAYOUT\|<bots>\|<tanks>\|<tankHealers>\|<raidHealers>\|<groups>` | Raid layout applied by `.bot commander bind <LeadBot> raid` (`<tanks>` main first, comma list; `<groups>` the real subgroups, e.g. `1=a,b;2=...`). `TBM:RAID_LAYOUT\|warning\|<text>` warns (a member not where planned) instead of an error. |

### Protocol Grammar & Semantics

- **Capabilities (`TBM:CAPS`):** Emits `TBM:CAPS|pull-seconds,flee,inventory,behavior,claimed-roster,loot-log,downtime,professions,blessing,blessing-player,engage,choice,focus-mark`, with `,guild-orders,guild-order-steps` appended while the order source index is ready (travel tables loaded via `AiPlayerbot.AutoDoQuests = 1`), `,guild-order-nobuy` appended while player Buy orders are refused (`AiPlayerbot.GuildOrderPlayerBuy = 0`) and `,dungeon-run` appended while the dungeon run is enabled. (`mimic` is a behavior toggle, not a capability token).
- **Professions (`TBM:PROF`/`TBM:RECIPE`):** `.bot professions <bot>` lists the bot's professions with skill (`TBM:PROF`), the recipes it knows (`TBM:RECIPE`, with the skill steps each colour starts at and an order field: `ok` or the reason), and the recipes it could still learn at its current rank (`TBM:RECIPE_MISSING`, with the recipe item that teaches it or `0` for trainer-only).
- **Loot history (`TBM:LOOT`):** `.bot lootlog [cursor]` replays loot rows for your controllable party bots (owned alts and hired companions) from the last 24 hours, newest piece first, up to 40 rows per reply. A money row carries the copper amount as `<item>` and the corpse entry as `<itemId>`; an item row carries the item name, id and quality. Send the returned `<nextCursor>` back for the next page; `expired` means the piece it pointed at was pruned, so start over without a cursor.
- **Guild bot rank (`TBM:ORDER_ACCESS`):** Carries the guild's bot rank and what it reaches.
  - `-|-|-|-|0|0` line: sent when `AiPlayerbot.GuildOrderBotRank = 0` (off), or when the guild has no unlocked bank (then also no tabs and no gold).
  - `<rankIndex>` the rank every bot holds (by default the guild's lowest rank); `<rankName>` its name.
  - `<tabsOk>`/`<tabsDenied>`: 1-based bank tabs it reaches or not (`-` when empty).
  - `<gold 0/1>`: whether it may take guild gold (only ranks 0-1 may).
  - `<canSetRank 0/1>`: whether you may change the rank (your rank must allow promoting and demoting members).
  - Sent first inside every `ORDER_BEGIN`/`ORDER_END` list, again after `.bot order rank`, and as the answer to `.bot order access`.
- **Bot State Grammar (`TBM:BOTSTATE`):** Emitted for all controllable party bots (owned alts and hired companions). Format:
  ```text
  TBM:BOTSTATE|<bot>|move=<follow|stay|guard|free|flee|custom>,loot=<on|off>,aoe=<on|off>,autocc=<on|off>,savemana=<on|off>,boost=<on|off>,threat=<on|off>,potions=<on|off>,mimic=<on|off>,spread=<on|off>,hold=<on|off>,noblink=<on|off>[,offheal=<on|off>,offdps=<on|off>],sap=<on|off>[,specs=<strategy>+<strategy>...],focus=<mark>
  ```
  `offheal` and `offdps` are sent only for paladins, priests, shamans and druids. `specs` lists the spec strategies the bot fights with (for example `retribution`, `balance`, `holy+offdps`, `shadow+offheal`), so the addon shows the real spec. `focus` is the bot's focus mark (`.bot focus`; `skull` by default, `none` when the bot ignores marks); the `focus-mark` capability says the server sends it.
- **Raid Layout (`TBM:RAID_LAYOUT`):** `.bot commander bind <LeadBot> raid` sends one `TBM:RAID_LAYOUT|<bots>|<tanks>|<tankHealers>|<raidHealers>|<groups>` built from the real subgroups read back after the moves (never the plan alone); `<tanks>` lists the main tank first. Members not where planned ride a separate `TBM:RAID_LAYOUT|warning|<text>` line (a warning, not a `TBM:ACTION_ERR`). The bind ACK scope is `raid` for a raid bind (`party` otherwise).
- **CC Assignments (`TBM:CC_ASSIGN`):** Broadcasts assigned raid marks for controllable party bots (owned alts and hires). A bot is included only if it is in your party and has the `cc` combat strategy active with an assigned raid mark (including Moon; a mark assigned without the `cc` strategy active is excluded). Ungrouped alts are excluded.
- **Message Length Cap:** `TBM:VIEW` perception lines are packed to 250 bytes (within the WoW client addon message buffer limit of 255 bytes); `TBM:LOOT` rows shorten their two name fields to fit the same 250 bytes; other protocol lines send without truncation.
- **Delivery Guarantees:** Drained commands run in arrival order on the world tick before the bot update; deferred bot chat and packets queued ahead of each command run first.

Hand-typed `.bot` commands in chat continue to work identically alongside the addon.
