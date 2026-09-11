---
id: guide-living-world
title: Living World & Autonomous Bots
category: guides
summary: Comprehensive guide to the autonomous bot ecosystem, including wandering bots, quest grinding, AH economy, guild formation, and dungeon/BG participation.
tags: [guide, living-world, randombots, economy, questing, guilds, auction-house]
relates_to:
  - guide-configuration-tuning
  - guide-getting-started
  - concept-strategy-engine
---

# Living World & Autonomous Bots

TortoiseBots is not limited to player-owned companions. It includes a complete **Living World** subsystem that populates your realm with autonomous bots (`RNDBOT*`). These bots explore zones, grind mobs, gather profession nodes, quest, trade on the Auction House, form parties, and create guilds—making the world feel vibrant and active like a populated MMO server.

---

## 1. Autonomous Bot Lifecycle

```mermaid
flowchart TD
    Login["Login & Spawn at Inn / Bindpoint"] --> GearCheck["Check Gear Durability & Bag Space"]
    GearCheck -->|"Need Repair / Empty Bags"| Town["Visit Blacksmith & Vendor Junk"]
    GearCheck -->|"Ready to Venture"| GoalChoice{"Choose Activity Intent"}
    Town --> GoalChoice
    GoalChoice -->|Questing| Quest["Accept & Track Local Quests"]
    GoalChoice -->|Grinding| Grind["Path to Level-Appropriate Mob Camp"]
    GoalChoice -->|Gathering| Gather["Path to Herb & Ore Resource Nodes"]
    Quest --> Travel["Travel via Foot / Flight Master / Zeppelin"]
    Grind --> Travel
    Gather --> Travel
    Travel --> Combat["Engage Mobs with Class Rotations"]
    Combat --> Loot["Loot Mobs, Quest Items & Resources"]
    Loot --> Rest["Rest: Consume Food / Water"]
    Rest --> CheckBags{"Bags Full or Quest Done?"}
    CheckBags -->|Yes| Hearth["Hearthstone / Return to Town"]
    CheckBags -->|No| Combat
    Hearth --> TurnIn["Turn In Quests & Learn Spell Ranks"]
    TurnIn --> AH["Post Gathered Items on Auction House"]
```

## 2. Wandering & Leveling Bots

Autonomous bots roam the open world, reacting dynamically to nearby players and wildlife:

| Activity | How It Works in the World |
| :--- | :--- |
| **Zone Exploration & Travel** | Bots take flight paths, ride zeppelins and boats, run along roads between towns, and use hearthstones to return to inns. |
| **Grinding & Combat** | Bots seek out level-appropriate hostile mobs, pull with class-appropriate ranged abilities, execute standard rotations, and rest with food and water between fights. |
| **Questing & Progression** | Bots pick up quests from quest givers, track quest objectives (killing specific mobs or collecting items), and turn them in for XP and gold rewards. |
| **Gathering & Professions** | Bots with Herbalism or Mining will path toward nearby herb and ore nodes in the world to harvest them. |
| **Town Life & Immersion** | In towns, bots visit class trainers to learn new spell ranks, repair yellow/red durability gear at blacksmiths, vendor junk items, and wave or say hello when passing human players. |

---

## 3. Dynamic World Level Syncing

A common issue with bot realms is having level 60 bots everywhere while you are leveling a fresh level 10 character. TortoiseBots solves this through dynamic bracket scaling:

* **`AiPlayerbot.SyncLevelWithPlayers = 1`**
  * When enabled, the server dynamically caps random bot levels to the highest online human player's level + 5 (`SyncLevelMaxAbove = 5`).
  * If you log in at level 18, the bot population scales around levels 10–23, populating Westfall, Darkshore, the Barrens, and Loch Modan.
  * As you level up into the 30s and 40s, the bot population advances into Stranglethorn Vale, Tanaris, and the Hinterlands alongside you.

---

## 4. Social Interaction: Groups & Guilds

Random bots actively participate in realm social structures:

### Party & Dungeon Invites
* **Inviting Lone Players (`RandomBotInvitePlayer = 1`):** If you are questing solo in an area, nearby random bots on matching quests or grinding in the same camp will invite you to form a party. (If you prefer to solo, turning on `/dnd` stops bot invites).
* **Bot-to-Bot Grouping (`RandomBotGroupNearby = 1`):** Bots organically group up with nearby bots to tackle difficult quest mobs, elite areas, and dungeons.

### Autonomous Guild Formation (`RandomBotFormGuild = 1`)
* Bots periodically visit guild masters in capital cities, purchase a **Guild Charter**, and collect signatures from other unguilded bots.
* Once registered, bots create custom guild names, invite fellow bots and players, and display guild names over their heads.

---

## 5. The Living Auction House Economy (`AhMarketService`)

TortoiseBots features an active, simulated player economy that prevents the Auction House from feeling like a ghost town:

```mermaid
flowchart TD
    Loot["Wandering Bots Loot Mobs & Craft Gear"] --> Appraise["Appraise Item Quality (Vendor Junk vs AH Surplus)"]
    Appraise --> Post["Post Native Auction House Listings (Deposit + Duration)"]
    Post --> Trade["Players & Other Bots Browse, Bid, and Buyout"]
    Trade --> Mail["Native Settlement: Gold Delivered to In-Game Mailbox"]
```

* **Bot Sellers:** Bots list surplus profession mats (cloth, herbs, ore, leather), green/blue Bind-on-Equip (BoE) gear, and crafted consumables on the Auction House at realistic market prices.
* **Bot Buyers:** When bots accumulate gold, they periodically search the Auction House for gear upgrades suited to their class and spec. If an item on the AH is better than their current equipped gear, they place bids or buyout the listing.
* **Personal Settlement:** Items sold or bought by bots use native core auction mechanics. Human players receive real gold in their mailbox when a bot buys their auctions.

### Auction House Administration Commands (`.bot ah` / `.ahbot`)
Administrators can inspect and tune the synthetic market pass using in-game commands:
* `.bot ah status` — Inspect active listings, inventory counts, and market passes.
* `.bot ah reload` — Reload overrides and blacklists from the `ahbot_items` database table.
* `.bot ah rebuild [all]` — Triggers an immediate market pass (`all` expires active unbid synthetic items).
* `.bot ah item <id>` — Checks blacklist status or custom price overrides for an item.
* `.bot ah item <id> reset` — Removes custom override and restores formula pricing.
* `.bot ah item <id> <value> [chance] [min] [max]` — Overrides pricing or blacklists an item (`0 0`).

---

## 6. Automated Dungeon & Battleground Queues

### Looking-For-Trouble (LFT) Dungeon Autofill
* Config: **`AiPlayerbot.RandomBotLftEnabled = 1`**
* When human players queue in the LFT tool and sit waiting for missing roles (especially Tanks or Healers), the module checks idle random bots in the world matching that level bracket.
* Eligible bots auto-queue, accept the dungeon invite, and teleport into the instance to run the dungeon with the human group.

### Battleground Auto-Queue
* Config: **`AiPlayerbot.RandomBotBgEnabled = 1`**
* Monitors PvP queues for **Warsong Gulch (WSG)**, **Arathi Basin (AB)**, and **Alterac Valley (AV)**.
* When real players queue up, random bots queue to balance faction team sizes and launch the battleground, allowing you to play active PvP battlegrounds even on low-population private servers.

---

## 7. Recommended Living World Configuration

To enable a full living world on your server, ensure these toggles are set in `conf/aiplayerbot.conf`:

```ini
# Master bot toggle
AiPlayerbot.Enabled = 1

# Random bot population pool
AiPlayerbot.RandomBotAutologin = 1
AiPlayerbot.RandomBotAutoCreate = 1
AiPlayerbot.MinRandomBots = 60
AiPlayerbot.MaxRandomBots = 150

# Dynamic level scaling with online players
AiPlayerbot.SyncLevelWithPlayers = 1
AiPlayerbot.SyncLevelMaxAbove = 5

# Social immersion
AiPlayerbot.RandomBotInvitePlayer = 1
AiPlayerbot.RandomBotGroupNearby = 1
AiPlayerbot.RandomBotFormGuild = 1
AiPlayerbot.EnableGreet = 1

# Optional living economy & queues
AiPlayerbot.AhMarketEnabled = 1
AiPlayerbot.RandomBotLftEnabled = 1
AiPlayerbot.RandomBotBgEnabled = 1
```
