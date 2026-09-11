---
id: guide-getting-started
title: Getting Started with TortoiseBots
category: guides
summary: How to claim, spawn, invite, and adventure with companion bots from your own account.
tags: [guide, player, quickstart, roster, party]
relates_to:
  - guide-player-controls
  - guide-configuration-tuning
  - class-overview
---

# Getting Started with TortoiseBots

TortoiseBots allows you to turn characters on **your own account** into intelligent companion bots to explore the open world, level together, and run 5-player dungeons.

## 1. Prerequisites

1. Create the characters you want to use as bots on your account.
2. Install the **[TortoiseBotsManager](https://github.com/tortoise-wow-stack/TortoiseBotsManager)** addon (`/tbm`) into your client's `Interface/AddOns/` directory.
3. Log into your main player character in the game.

## 2. Spawning Your Bots

You can manage your bots using the in-game UI (`/tbm`) or through native `.bot` chat commands.

### In-Game Addon (`/tbm`)
1. Type `/tbm` to open the control panel.
2. Switch to the **Roster** tab.
3. You will see characters from your account listed. Select a character and click **Login** / **Invite**.
4. The bot will appear as a headless session and join your party!

### Command-Line Shortcuts
Alternatively, type these commands in chat:
```text
.bot add <CharacterName>       # Log in an owned character as a bot
.bot invite <CharacterName>    # Invite bot to your party
.bot summon                    # Summon nearby party bots to your location
```

> [!NOTE]
> **Human Reclaim:** If you want to play a character yourself that is currently running as a bot, simply log into it from the character select screen. The server will cleanly disconnect the bot and let you log in normally.

> [!TIP]
> **Fast Leveling & Training:**
> * Set `AiPlayerbot.SyncAltLevelToMaster = 1` in `conf/aiplayerbot.conf` so all bot characters on your account automatically level up to match your main character.
> * Take your bots to class trainers in major cities and whisper them `trainer` (or type `.bot command <Name> trainer`) to have them learn all available class spells and ranks in one click!

## 3. Building a Balanced 5-Man Party

For dungeons and elite quests, aim for standard group balance:
- **1 Tank:** Protection Warrior, Feral Bear Druid, or Protection Paladin.
- **1 Healer:** Holy Priest, Restoration Shaman, Holy Paladin, or Restoration Druid.
- **3 DPS:** Balanced mix of melee (Rogue, Fury Warrior) and ranged (Mage, Hunter, Warlock).

## 4. Basic Controls in the Field

Once your bots are in party:
- **Follow:** Bots automatically follow their party leader. If a bot gets stuck, use `.bot summon` or the **Summon** button in `/tbm`.
- **Resting:** Bots will automatically sit and eat food or drink water when out of combat when low on health or mana.
- **Looting:** Bots participate in party loot rolls and can loot quest items according to standard party loot rules.
