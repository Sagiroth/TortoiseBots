---
id: guide-configuration-tuning
title: Configuration Knobs & Feature Flags
category: guides
summary: Complete guide to customization knobs, player quality-of-life flags, world immersion settings, and in-game tactical toggles.
tags: [guide, config, tuning, settings, feature-flags, knobs]
relates_to:
  - guide-getting-started
  - guide-player-controls
  - guide-observability-dashboard
  - concept-strategy-engine
---

# Configuration Knobs & Feature Flags

TortoiseBots provides a rich set of feature flags and tuning knobs. Whether you are running a solo private server or hosting a community realm, these settings allow you to customize bot intelligence, party convenience, world immersion, and economy.

Configuration lives in two files:
1. `conf/tortoise_bots.conf` — Native module options, diagnostic levels, and telemetry.
2. `conf/aiplayerbot.conf` — Gameplay feature flags, QoL toggles, AI thresholds, and services.

---

## 1. Player Quality-of-Life (QoL) Flags

These settings dramatically enhance the solo or small-group experience with owned bots:

| Setting | Default | Recommended | What It Does |
| :--- | :---: | :---: | :--- |
| `AiPlayerbot.SyncAltLevelToMaster` | `0` | **`1`** | **Auto-Level Bot Alts:** When enabled, all bot characters on your account automatically level up to match your main character's level as you progress. |
| `AiPlayerbot.BoostFollow` | `0` | **`1`** | **Sprint to Catch Up:** Bots temporarily increase run speed when they fall behind the leader, preventing them from trailing or getting caught on dungeon geometry. |
| `AiPlayerbot.NonGmFreeSummon` | `0` | **`1`** | **Unrestricted Summoning:** Allows regular players without GM status to use `.bot summon` to gather their bots out of combat anywhere. |
| `AiPlayerbot.AutoLearnQuestSpells` | `1` | **`1`** | **Class Quest Rewards:** Automatically teaches spells awarded by completed class quests (e.g. Paladin Resurrection, Warlock pet summons, Shaman totems). |
| `AiPlayerbot.RollBadItemsWithPlayer` | `0` | **`1`** | **Need on Empty Slots:** Forces party bots to roll Need on dungeon drops if their corresponding equipment slot is empty or severely under-leveled. |
| `AiPlayerbot.RandomGearUpgradeEnabled` | `1` | **`1`** | **Automatic Gear Scaling:** Periodically equips bots with level-appropriate dungeon and quest gear as they level up. |

---

## 2. World Population & Immersion Flags

These flags control the behavior of autonomous random bots roaming the world:

| Setting | Default | Recommended | What It Does |
| :--- | :---: | :---: | :--- |
| `AiPlayerbot.RandomBotAutologin` | `0` | **`1`** | Automatically logs in random bots (`RNDBOT*`) at server startup. |
| `AiPlayerbot.RandomBotAutoCreate` | `0` | **`1`** | Automatically creates new bot accounts/characters if the active pool is below `MinRandomBots`. |
| `AiPlayerbot.MinRandomBots` / `MaxRandomBots` | `0` | `50` / `150` | Sets the minimum and maximum active random bot population. |
| `AiPlayerbot.SyncLevelWithPlayers` | `0` | **`1`** | **Dynamic Level Bracket:** Restricts random bot levels to the highest online human player level + 5, ensuring the world levels up alongside you. |
| `AiPlayerbot.RandomBotInvitePlayer` | `1` | `1` | Random bots in the open world will invite solo human players to form questing groups. |
| `AiPlayerbot.RandomBotGroupNearby` | `1` | `1` | Bots will organically invite each other to form questing parties and dungeon groups. |
| `AiPlayerbot.RandomBotFormGuild` | `1` | `1` | Bots will buy guild charters, collect signatures from other bots, and found their own guilds. |
| `AiPlayerbot.EnableGreet` | `1` | `1` | Bots wave or say hello when passing players in towns and roads. |
| `AiPlayerbot.RandomBotShowHelmet` / `ShowCloak`| `1` | `1` | Renders helmets and cloaks on bots. |

---

## 3. Autonomous Services (Default OFF)

All autonomous services are fully bounded and disabled by default. Enable only the services you need:

| Service Flag | Default | Description |
| :--- | :---: | :--- |
| `AiPlayerbot.RandomBotLftEnabled = 1` | `0` | **LFT Dungeon Autofill:** When real players queue for Looking-For-Trouble dungeons and wait for missing roles (e.g. Tank or Healer), eligible bots fill the vacant slots and run the instance. |
| `AiPlayerbot.AhMarketEnabled = 1` | `0` | **Living Auction House:** Bots post gathered trade goods and bind-on-equip gear on the Auction House, and bid on/buyout items using real player pricing models. |
| `AiPlayerbot.RandomBotBgEnabled = 1` | `0` | **Battleground Auto-Queue:** Injects random bots into Warsong Gulch, Arathi Basin, and Alterac Valley when human players queue. |

---

## 4. Combat & Reaction Thresholds

Fine-tune how aggressively bots heal, rest, or drink:

| Setting | Default | Tuning Guidance |
| :--- | :---: | :--- |
| `AiPlayerbot.CriticalHealth` | `25` | Percent health considered an emergency. Triggers *Lay on Hands*, *Last Stand*, *Shield Wall*, or *Divine Shield*. |
| `AiPlayerbot.LowHealth` | `50` | Percent health triggering prioritized heavy heals (*Greater Heal*, *Healing Wave*). Increase to `65` for safer dungeon runs. |
| `AiPlayerbot.MediumHealth` | `70` | Threshold for maintenance heals (*Renew*, *Rejuvenation*, *Flash Heal*). |
| `AiPlayerbot.AlmostFullHealth` | `90` | Health ceiling above which bots stop casting heals to conserve mana. |
| `AiPlayerbot.LowMana` | `20` | Mana floor where casters switch to low-cost wanding or conserve mana. |
| `AiPlayerbot.MediumMana` | `50` | Threshold where bots consider conservative rotations. |

---

## 5. In-Game Live Toggles (On the Fly via `/tbm` or Chat)

You do not need to restart the server to adjust tactical behavior during gameplay. You can toggle these anytime:

### Tactical Gameplay Toggles (`.bot action <toggle>`)
- `.bot action aoe <on|off>` — Enables or disables Area-of-Effect abilities (crucial around crowd-controlled mobs).
- `.bot action pullback` — Orders the tank to pull the target and sprint back to your location.
- `.bot action focus` — Focuses party damage onto the mob marked with the Skull raid marker.
- `.bot action cc <mark>` — Assigns CC to a specific raid marker (e.g. Moon, Star).

### Strategy & Stance Toggles
Using `.bot strategy <+|-strategy>` or the `/tbm` addon:
- `+conserve mana` / `-conserve mana` — Restricts casters to basic, high-efficiency spells.
- `+loot` / `-loot` — Toggles whether bots run to loot corpses after combat.
- `+silent` / `-silent` — Silences bot chatter in party/say chat so they execute commands quietly.
- `+passive` / `-passive` — Halts all bot attacks; bots will only follow and hold fire.
- `.bot formation <arrow|line|circle|shield>` — Changes follow positioning around the leader.
