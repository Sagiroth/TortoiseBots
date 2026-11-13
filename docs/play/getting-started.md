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

TortoiseBots fills the realm with bots and lets you take some of them along: your own alts, hired companions, or bots you invite into your guild. This page gets your first party going.

## What you need

- A character to play, and a few more characters (alts) on the same account that you want as bots.
- The [TortoiseBotsManager](commands/addon.md) addon (`/tbm`) in your client's `Interface/AddOns/` folder. Optional, but much easier than typing commands.
- A server that runs TortoiseBots.

## The Kinds of Bots

| Kind | What it is | How you get one | When you are done with it |
| :--- | :--- | :--- | :--- |
| **Pool bot** | A bot that lives in the world: levels, quests, grinds, sells on the auction house. | You don't. It may invite you to its group (nearby, similar level, and never while you are `/dnd`) or fill your dungeon finder (LFT) group when roles are missing. | It carries on with its own life. |
| **Own alt** | A character on your own account, played by the server. | `.bot add <Name>` or **Login** in `/tbm`. | `.bot remove <Name>` (or `.bot rm`) logs it out. If it just leaves your group, it starts playing on its own ([alt-bot](#when-your-alt-leaves-the-group)). |
| **Hired companion** | A temporary character made for you, at your level. | Talk to a `<Mercenary Hire>` recruiter in an inn or a capital. Costs gold (about 1.5 gold at level 60, less at low level; each extra hire costs more). Up to 4 at once by default (the server can raise this to 39 for raids). | Deleted when you dismiss it, it leaves your party, or you stay logged out past the grace time (5 minutes by default). |
| **Guild bot** | A pool bot you invited into your guild. | `/ginvite <Name>` on a pool bot of your faction with no guild (see the conditions below). | It stays in your guild, keeps its own life, and you can command it like your own alt. See [Guild bots](#guild-bots). |

## Your first party

| Step | With the addon | With chat commands |
| :--- | :--- | :--- |
| 1. Log in your alt | `/tbm` → **Roster** tab → select the alt → **Login** | `.bot add <Name>` |
| 2. Invite it | **Invite** | `.bot invite <Name>` |
| 3. Bring it to you | **Summon** | `.bot summon <Name>` |
| 4. Log it out later | **Logout** | `.bot remove <Name>` |

- The alt logs in where you last left it and follows you.
- Want to play the alt yourself? Just log into it from the character screen; the bot session ends by itself.
- Missing a class? Hire one at a recruiter. Hired companions join your party by themselves. Full details: [Roster and Hiring Commands](commands/roster-and-hiring.md#bot-hire).

## When your alt leaves the group

> On a default server an own alt that **leaves your group** (you kick it, it leaves, or the group breaks up) does not stop: it becomes an **alt-bot** and plays on its own like a pool bot. If it is level 10 or higher it is first teleported to a zone that fits its level, never a capital (an alt left in an instance, a battleground or on a boat leaves through the instance entrance portal, or to its battleground entry or a graveyard, first).

| You want to... | Do this |
| :--- | :--- |
| Stop it playing | `.bot remove <Name>` (logs it out) |
| Take it back as your companion | `.bot add <Name>`, or invite it into your group again (only your own group's invite reclaims it) |
| Play it yourself | Log into it from the character screen |

- It keeps playing while you are logged out, until the server restarts ([`AiPlayerbot.AltBotsLogoutWithOwner`](../server/settings/party.md#aiplayerbotaltbotslogoutwithowner) = `1` logs it out with you).
- When it stops, it whispers you what it did: levels, quests, deaths, gold.
- It plays carefully with your character: no auction house, mail, trade or guild bank; its own automation never destroys items, and it sells only grey and unneeded white items.
- Hardcore characters never become alt-bots.
- This is off by default. The server switches it on with [`AiPlayerbot.AltBots`](../server/settings/party.md#aiplayerbotaltbots) = `1`; while it is off, the alt just stands where you left it.

More: [Alt-Bots](commands/roster-and-hiring.md#alt-bots-autonomous-alts).

## Guild bots

Invite a pool bot into your guild with `/ginvite <Name>`. It joins when it has no guild, is your faction, is not hired, is not more than 30 levels above you and is not queued for a battleground or dungeon (a full guild also refuses). From then on:

- **It is yours to command:** whispers, `.bot` commands, trade, gear, summon, the same as your own alts. Only you (the player who invited it) get this, not the rest of the guild.
- **It still lives its own life** when you do not use it: questing, levelling, selling junk.
- **It keeps its gear for your raids:** at level 60 it no longer swaps gear or sells anything to vendors by itself; while levelling it never sells blue or epic items.
- **It survives pool resets**, and shows in the **Guild** tab of `/tbm`.
- **It takes [guild orders](guild-orders.md):** farm, buy or craft items for the guild bank.
- Removing it from the guild ends all of this.

## Levelling, spells and gear

| | Own alt / alt-bot | Hired companion | Pool bot / guild bot |
| :--- | :--- | :--- | :--- |
| **Class spells** | Train it yourself: take it to its class trainer, **target the trainer**, whisper `trainer` (it pays with its own gold) | Free, learned at every level-up | Free, learned at every level-up |
| **Levels** | Earned by playing. Optional: [`AiPlayerbot.SyncAltLevelToMaster`](../server/settings/party.md#aiplayerbotsyncaltleveltomaster) levels it up to you while grouped (off by default) | Hired at your level (or at the level you asked for) | Earned by playing |
| **Gear** | What you give it; it also equips upgrades it loots | Comes with a kit; at level 60 a gear tier set ([contracts](commands/roster-and-hiring.md#mercenary-contracts)) | Starter outfit once, then loot, quests and the auction house |
| **Loot rolls** | Rolls, never Needs against you | Never loots, always passes (off by default) | Rolls, never Needs against you |

## Building a balanced 5-man party

One tank, one healer, one melee damage dealer and two ranged damage dealers:

| Role | Good picks | What they bring |
| :--- | :--- | :--- |
| **Tank** | Protection Warrior, Bear Druid, Protection Paladin | Holds threat, taunts, interrupts |
| **Healer** | Holy Priest, Restoration Shaman, Holy Paladin, Restoration Druid | Healing and dispels |
| **Melee DPS** | Combat or Assassination Rogue, Fury Warrior, Cat Druid | Burst, interrupts, Sap before the pull |
| **Ranged DPS** (2) | Frost or Fire Mage, Marksmanship Hunter, Affliction Warlock | Polymorph, Freezing Trap, Banish, conjured food and water |

## Basic controls in the field

The buttons are in `/tbm` (Summon is on the **Roster** tab).

| You want to... | Button | Command | What the bots do |
| :--- | :--- | :--- | :--- |
| Bring a bot to you | **Summon** | [`.bot summon <Name>`](commands/roster-and-hiring.md#bot-summon) | The bot itself must be out of combat and alive: it appears about 5 yards from you after a 3-second portal. |
| Kill one target first | **Focus Skull** | [`.bot action focus skull`](commands/party-actions.md#bot-action-focus-skull) | Your target gets the Skull mark and every party bot attacks it. |
| Pull a pack around a corner | **Pullback** | [`.bot action pullback`](commands/party-actions.md#bot-action-pullback) | The tank pulls your target and runs back to you; the others wait at your spot until it arrives. Stand behind a corner first. |
| Stop moving | **Stay** | [`.bot action stay`](commands/party-actions.md#bot-action-stay) | Bots hold their position. |
| Follow again | **Follow** | [`.bot action follow`](commands/party-actions.md#bot-action-follow) | Bots follow you in formation. |
| Crowd-control a mob | **CC Mark** | [`.bot action cc <mark>`](commands/party-actions.md#bot-action-cc) | A bot that can CC your target (Polymorph, Sap, Shackle...) keeps it out of the fight. |
| Protect crowd control | **AoE** (toggle) | [`.bot action aoe off`](commands/party-actions.md#bot-action-aoe) | Bots stop using area spells. |

## Where next

- [Dungeon & Raid Tactics](dungeons-and-raids.md): pulling, crowd control, wipes, loot.
- [What Bots Can Do](features.md): every feature and whether it is on.
- [Party Action Commands](commands/party-actions.md) and [Whisper Commands](commands/whispers.md): every order.
- [Known Limitations](known-limitations.md): what does not work yet.
