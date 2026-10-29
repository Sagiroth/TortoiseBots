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
2. Extract the folder into your game client's addon directory:
   ```text
   World of Warcraft/Interface/AddOns/TortoiseBotsManager
   ```
3. Ensure the folder is named exactly `TortoiseBotsManager` (not nested inside a duplicate folder).
4. Log into the game and type `/tbm` in chat to toggle the control interface.

---

## Addon Panels & Features

The interface is divided into functional tabs designed for different stages of gameplay:

### Roster Tab

- **Character List:** Lists all characters on your account, displaying class, offline/online/starting/removing, plus an in-party indicator. States come from the headless session plus lifecycle: `online` (live bot with an active session), `starting` (pending load or login), `removing`, otherwise `offline`.
- **Login & Logout:** Click **Login** to spawn an owned alt as a headless companion bot; click **Logout** to log it out cleanly.
- **Invite & Summon:** Group with online alts or summon them directly to your location with one click.
- **Alt-Bot Indicators:** Highlights alts playing autonomously in the world, allowing you to reclaim them or log them out remotely.

### Party Tab

- **Movement Controls:** Quick buttons for movement orders such as follow, stay, come, and flee.
- **Combat Orders:** Direct buttons to issue attack, pull, and pullback maneuvers with configurable pull delay sliders.
- **Crowd Control:** Assign crowd control raid marks (Moon, Star, Diamond, etc.) to capable party bots.
- **Live Toggles:** Quick switches for party-wide AoE, corpse looting, consumable mimicry, and smart auto CC.

### Bot Panel Tab

- **Gear Inspector:** View equipped items and durability across paperdoll slots on owned alts and hired companions.
- **Bag Management:** Browse carried inventory items, identify equippable upgrades, and equip or unequip items remotely.
- **Behavior Toggles:** Configure individual toggles for threat backoff, mana conservation, offensive cooldowns, and potion usage.
- **Totem Manager:** For shaman companions, pin totem choices per element and trigger immediate totem drops.

### Guild Tab

- Displays claimed guild bots for your account, detailing bot levels and grouping state.

---

## Addon Command Transport (Player View)

The addon talks to the server over private addon messages rather than standard chat channels.

- **Silent Operation:** Commands sent by clicking addon buttons travel as addon messages instead of `.bot` chat text, and server replies return as addon messages; the module consumes its own prefixed payloads so they never reach group chat.
- **Chat Fallback:** If the private addon channel reports `none` (for example, while ungrouped, in a battleground group with no pre-battleground group, or on an older server build that never sends the trailer), the addon falls back to sending standard `.bot` chat commands.
- **Execution Order & Queueing:** Commands sent via addon-channel messages and typed `.bot` chat share a single per-requester FIFO queue drained on the world thread. Because packet delivery and chat dispatch can resolve asynchronously, send order does not guarantee receive order across different channels. Addon commands execute on the server's next world tick, before bots update.
- **Movement Conflict Resolution:** A newer `.bot` movement order drops older chat-queued movement orders (`follow`, `stay`, `guard`, `free`, `flee`, `wander`) queued inside the bot AI; it does not cancel pending `.bot` queue entries.
- **Exemptions & Teardown:** Action packets the AI handles inline on the world thread (its own casts, hire provisioning) are processed directly and exempt from command queuing. Queued bot chat and packets run in arrival order ahead of each `.bot` command drained on the world tick. Even when the module is globally disabled (`AiPlayerbot.Enabled=0`), the drain still runs and queued orders execute on the world thread before the bot update.
- **Rate Limiting:** The server queues up to 128 bot commands per player; commands beyond the cap are dropped and reported once per tick with the exact message: `Too many bot commands at once: <n> ignored.`

---

## Synchronization Streams

The module transmits structured protocol lines (prefixed with `TBM:`) to keep the addon interface synchronized with server state. Detailed stream definitions and grammar:

| Protocol Stream | Description |
| :--- | :--- |
| `TBM:TRANSPORT\|<channel>` | Reports the addon command channel (`party` or `none`). |
| `TBM:ROSTER_BEGIN\|<n>` / `TBM:ROSTER_END` | Delimits the account character roster stream. |
| `TBM:ROSTER\|<guid>\|<name>\|<class>\|<state>\|<grouped>\|<loc>` | Emits one owned character row. |
| `TBM:CLAIMED_BEGIN\|<n>` / `TBM:CLAIMED_END` | Delimits the claimed guild bots snapshot. |
| `TBM:CLAIMED\|<guid>\|<name>\|<class>\|<level>\|<state>\|<grouped>\|<loc>\|<locked>` | Emits one claimed bot row. |
| `TBM:BOTSTATE_BEGIN\|<n>` / `TBM:BOTSTATE_END` | Delimits live movement and toggle states for controllable party bots. |
| `TBM:BOTSTATE\|<bot>\|<fields>` | Emits bot movement mode, 9 behavior toggles, and the sap switch. |
| `TBM:CC_ASSIGN_BEGIN\|<n>` / `TBM:CC_ASSIGN_END` | Delimits active crowd control assignments. |
| `TBM:CC_ASSIGN\|<bot>\|<mark>` | Emits a party bot's assigned CC raid mark. |
| `TBM:CAPS\|<capabilities>` | Advertises server feature capabilities. Exact tokens: `pull-seconds,flee,inventory,behavior,claimed-roster,loot-log,downtime`, plus `dungeon-run` while the dungeon run is enabled (`AiPlayerbot.DungeonSelfClear = 1`). |
| `TBM:LOOT_BEGIN\|<n>\|<nextCursor>` / `TBM:LOOT_END\|<nextCursor>` | Delimits one page of loot history from `.bot lootlog`. |
| `TBM:LOOT\|<ts>\|<bot>\|<event>\|<itemId>\|<item>\|<sourceType>\|<source>\|<quality>` | Emits one loot row (item taken or money taken). |
| `TBM:ALTBOTS\|<names>` | Comma-separated list of owned characters active as autonomous alt-bots. |
| `TBM:VERSION\|<version>` | Handshakes server module version (e.g. `2026-10-20-v1` or `dev`). |
| `TBM:VIEW` | Real-time perception frame lines for Remote Commander. |
| `TBM:ACTION_ACK\|<intent>\|<scope>\|<count>\|<detail>` | Confirms action execution (`<scope>` is `party` or `bot:<Name>`). |
| `TBM:ACTION_ERR\|<intent>\|<code>\|<message>` | Notifies action failure with structured error code. |

### Protocol Grammar & Semantics

- **Capabilities (`TBM:CAPS`):** Emits `TBM:CAPS|pull-seconds,flee,inventory,behavior,claimed-roster,loot-log,downtime`, with `,dungeon-run` appended while the dungeon run is enabled. (`mimic` is a behavior toggle, not a capability token).
- **Loot history (`TBM:LOOT`):** `.bot lootlog [cursor]` replays loot rows for your controllable party bots (owned alts and hired companions) from the last 24 hours, newest piece first, up to 40 rows per reply. A money row carries the copper amount as `<item>` and the corpse entry as `<itemId>`; an item row carries the item name, id and quality. Send the returned `<nextCursor>` back for the next page; `expired` means the piece it pointed at was pruned, so start over without a cursor.
- **Bot State Grammar (`TBM:BOTSTATE`):** Emitted for all controllable party bots (owned alts and hired companions). Format:
  ```text
  TBM:BOTSTATE|<bot>|move=<follow|stay|guard|free|flee|custom>,loot=<on|off>,aoe=<on|off>,autocc=<on|off>,savemana=<on|off>,boost=<on|off>,threat=<on|off>,potions=<on|off>,mimic=<on|off>,spread=<on|off>,sap=<on|off>[,specs=<strategy>+<strategy>...]
  ```
  `specs` lists the spec strategies the bot fights with (for example `retribution`, `balance`, `holy+offdps`), so the addon shows the real spec.
- **CC Assignments (`TBM:CC_ASSIGN`):** Broadcasts assigned raid marks for controllable party bots (owned alts and hires). A bot is included only if it is in your party and has the `cc` combat strategy active with an assigned raid mark (including Moon; a mark assigned without the `cc` strategy active is excluded). Ungrouped alts are excluded.
- **Message Length Cap:** `TBM:VIEW` perception lines are packed to 250 bytes (within the WoW client addon message buffer limit of 255 bytes); `TBM:LOOT` rows shorten their two name fields to fit the same 250 bytes; other protocol lines send without truncation.
- **Delivery Guarantees:** Drained commands run in arrival order on the world tick before the bot update; deferred bot chat and packets queued ahead of each command run first.

Hand-typed `.bot` commands in chat continue to work identically alongside the addon.
