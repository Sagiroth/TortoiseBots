---
id: play-getting-started
title: Getting Started with TortoiseBots
category: play
summary: How to set up your client, spawn companion bots from your own account, hire mercenaries, and adventure together.
tags: [getting-started, quickstart, roster, party, alts, addon]
relates_to:
  - play-commands-roster-and-hiring
  - play-commands-party-actions
  - play-commands-addon
  - play-dungeons-and-raids
  - server-settings-party
---

# Getting Started with TortoiseBots

TortoiseBots turns characters on your own account into intelligent companion bots to explore the open world, level together, and run 5-player dungeons. This guide walks you through setting up the companion addon, spawning your first bots, and leading a balanced party into the field.

TortoiseBots is designed for a player who plays solo on their realm. The world is populated by autonomous bots who level, travel, and trade, while you can bring your own characters or hire temporary mercenaries to tackle group content.

## The Kinds of Bots

TortoiseBots features three distinct categories of bots in the game world:

| Kind | What it is | Persistence |
| :--- | :--- | :--- |
| **Pool bots** | Random characters levelling and wandering the open world. | Permanent characters that live in the world database. |
| **Own alts** | Characters you created on your own player account. | Permanent characters fully owned and customized by you. |
| **Hired companions** | Mercenary companions hired for a play session. | Temporary characters deleted after dismissal or logout grace. |

<details>
<summary>How bot types differ in gameplay</summary>

Pool bots represent the living realm: they level, quest and grind on their own, join your dungeon finder (LFT) group when it is missing a role, and may invite you to their group. They are never your companions: for that, use your own alts or hire someone.

Your own alts are secondary characters on your account. When spawned with `.bot add`, the server starts a headless session for that character and sets it to follow you. You can then invite it into your group with `.bot invite <CharacterName>`, swap gear, train spells, set talents, and log into them directly as a human player at any time.

Hired companions are generated on demand at capital recruiters or mercenary inns to match your character level. When dismissed or when you leave the party, they are cleanly removed and never pollute the organic world pool.

</details>

## Quick Setup Workflow

```mermaid
flowchart LR
    CreateAlt["1. Create Alts on Your Account"] --> LogMain["2. Log Main Character"]
    LogMain --> OpenTBM["3. Open /tbm (Roster Tab)"]
    OpenTBM --> SelectRow["4. Select Alt & Click Login"]
    SelectRow --> InviteParty["5. Invite Alt to Party"]
    InviteParty --> Field["6. Adventure & Dungeons"]
```

### Prerequisites

1. Create the characters you want to use as bots on your account at the character select screen.
2. Download and install the [TortoiseBotsManager](commands/addon.md) addon into your client's `Interface/AddOns/` directory.
3. Log into your main character in the game world.

## Spawning Your Bots

You can manage your bots using the in-game addon interface or through native chat commands.

### In-Game Addon (`/tbm`)

1. Type `/tbm` in chat to open the management panel. See [TortoiseBotsManager Addon](commands/addon.md) for full controls.
2. Open the **Roster** tab.
3. Select an alt character from the list and click **Login**, then click **Invite** to add them to your party.
4. The alt enters the world as a headless session and joins your party.

### Command-Line Shortcuts

You can also control bot logins directly using chat commands:

```text
.bot add <CharacterName>
.bot invite <CharacterName>
.bot summon <CharacterName>
.bot remove <CharacterName>
```

See [Roster and Hiring Commands](commands/roster-and-hiring.md) for full command syntax.

> [!NOTE]
> **Human Reclaim:** If you want to play an alt character yourself while it is currently running as a bot, simply log into it from the character select screen. The server automatically disconnects the bot session and logs your human client in normally.

### Progression and Training Tips

- **Catching up in level:** off by default. When the server turns on [`AiPlayerbot.SyncAltLevelToMaster`](../server/settings/party.md#aiplayerbotsyncaltleveltomaster), your own alts in your group (with you as party leader) level up one level at a time until they reach your level.
- **Spell Training:** Your own alts learn spells the normal way. Take the alt to a trainer of its class, target the trainer yourself, and whisper `trainer` to the bot (or use `.bot command <Name> trainer`). It learns every rank it can afford, paying with its own gold. Hired companions do not need this: they learn their class spells for free whenever they level up. See [Whisper Commands](commands/whispers.md#w--trainer).
- **Hiring Mercenaries:** If you lack alts of a specific class, visit a <Mercenary Hire> recruiter at an inn or capital city and hire a temporary companion at your level. See [Roster and Hiring Commands](commands/roster-and-hiring.md#bot-hire) and [Hiring Settings](../server/settings/hiring.md).

## Building a Balanced 5-Man Party

A standard dungeon group consists of one tank, one healer, one melee DPS, and two ranged DPS. Example balanced compositions:

| Role | Recommended Classes & Specs | Primary Responsibilities | Tactical Strengths |
| :--- | :--- | :--- | :--- |
| **Tank** (1) | Protection Warrior, Feral Bear Druid, Protection Paladin | Holds primary threat and positions packs. | High mitigation, taunt mechanics, and melee interrupts. |
| **Healer** (1) | Holy Priest, Restoration Shaman, Holy Paladin, Resto Druid | Maintains party health and dispels debuffs. | Triage healing, dispel coverage, and party recovery. |
| **Melee DPS** (1) | Combat / Assassination Rogue, Fury Warrior, Feral Cat Druid | Focuses single-target burst, interrupts, and utility. | Physical burst, interrupts, stealth Sap, and Lockpicking. |
| **Ranged DPS** (2) | Frost / Fire Mage, Marksmanship Hunter, Affliction Warlock | Deals ranged burst and provides crowd control. | Polymorph, Freezing Trap, Banish, and conjured food/water. |

## Basic Controls in the Field

Use these essential actions to direct your bots during dungeon runs and combat:

| Intent / Action | Addon Button | Chat Command | Bot Behavior |
| :--- | :--- | :--- | :--- |
| **Regroup / Unstuck** | **Summon** | [`.bot summon <Name>`](commands/roster-and-hiring.md#bot-summon) | Teleports an out-of-combat bot to a safe position near you (about 5 yards away, 3-second portal). |
| **Focus Target** | **Focus Skull** | [`.bot action focus skull`](commands/party-actions.md#bot-action-focus-skull) | Orders all party bots in scope including healers to prioritize the Skull-marked target. |
| **Pullback Maneuver** | **Pullback** | [`.bot action pullback`](commands/party-actions.md#bot-action-pullback) | Orders the tank to pull from range and retreat behind a corner. |
| **Hold Position** | **Stay** | [`.bot action stay`](commands/party-actions.md#bot-action-stay) | Halts bot movement to prevent adds. |
| **Resume Follow** | **Follow** | [`.bot action follow`](commands/party-actions.md#bot-action-follow) | Resumes normal follow formation behind the party leader. |
| **Crowd Control** | **CC Mark** | [`.bot action cc <mark>`](commands/party-actions.md#bot-action-cc) | Assigns a bot with matching CC spells to disable a marked target. |
| **Disable AoE** | **AoE Off** | [`.bot action aoe off`](commands/party-actions.md#bot-action-aoe) | Disables area spells to protect active crowd control. |

For detailed dungeon mechanics, LoS corner pulls, and raid strategies, read the [Dungeon & Raid Tactics Guide](dungeons-and-raids.md). For complete command syntax, see [Party Action Commands](commands/party-actions.md).
