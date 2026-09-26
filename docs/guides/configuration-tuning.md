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

Configuration lives in two installed files (the build generates them from templates):
1. `aiplayerbot.conf`, installed next to `mangosd.conf` — every `AiPlayerbot.*` gameplay flag, QoL toggle, AI threshold and service. Template: `ai/playerbot/aiplayerbot.conf.dist.in`. A different path can be set with `AiPlayerbot.ConfigFile` in `mangosd.conf`.
2. `modules/tortoise_bots.conf` in the server config directory — `TortoiseBots.*` module options (log level, telemetry). Template: `conf/tortoise_bots.conf.dist`.

Both are read once at server start; `.reload config` only re-applies `TortoiseBots.LogLevel`. A line that is commented out (`#`) in the template sets nothing — the code default listed below applies. The Docker stack (`tortoise-docker-penqle`) renders these files from its `.env` on every start, so its values win there.

---

## 1. Player Quality-of-Life (QoL) Flags

These settings dramatically enhance the solo or small-group experience with owned bots:

| Setting | Default | Recommended | What It Does |
| :--- | :---: | :---: | :--- |
| `AiPlayerbot.SyncAltLevelToMaster` | `0` | **`1`** | **Auto-Level Bot Alts:** While an owned (non-random) bot is grouped with you and you are the group leader, it gains one level per AI update until it matches your level. |
| `AiPlayerbot.BoostFollow` | `0` | **`1`** | **Mount Up to Catch Up:** Bots far behind the leader (beyond react distance) trigger a mount check so they ride to catch up instead of trailing on foot. No speed hack; combat-safe. |
| `AiPlayerbot.NonGmFreeSummon` | `0` | **`1`** | **Unrestricted Summoning:** Allows regular players without GM status to use `.bot summon` to gather their bots out of combat anywhere. |
| `AiPlayerbot.AutoLearnQuestSpells` | `1` | **`1`** | **Class Quest Rewards:** Automatically teaches spells awarded by completed class quests (e.g. Paladin Resurrection, Warlock pet summons, Shaman totems). |
| `AiPlayerbot.AutoLearnTrainerSpells` | `0` | **`0`** | **Free Trainer Spells (random pool only):** When on, random bots learn every green-eligible trainer spell on level-up (Dual Wield at live data level, rank upgrades, poisons). When off, no free sweep runs — but paid trainer visits with gold still teach. Owned bots never get free spells either way, but for owned hunters/warlocks this flag also enables automatic pet initialization. |
| `AiPlayerbot.AutoLearnDroppedSpells` | `0` | **`0`** | **Level-60 Book Spells (random pool only):** Teaches dungeon/raid book spells the bot reached the level for. Same random-only scope as the trainer sweep. |
| `AiPlayerbot.RollBadItemsWithPlayer` | `0` | **`1`** | **Need on Empty Slots:** Forces party bots to roll Need on dungeon drops if their corresponding equipment slot is empty or severely under-leveled. |
| `AiPlayerbot.RandomGearUpgradeEnabled` | `1` | **`1`** | **Fresh-Bot Gear Seeding:** Gives a random bot a full set of level-appropriate gear once, on its first login (played time 0). It never overwrites earned gear; veteran bots upgrade through loot, the AH and respecs. |
| `AiPlayerbot.GenerateItemCaches` | `1` | **`1`** | **First-Boot Gear Caches:** Builds the `ai_playerbot_equip_cache` and `ai_playerbot_rnditem_cache` tables once, while they are empty, and loads them from the database afterwards. Leave it on for a fresh install — with empty caches bots only fill empty slots from loot and never judge an upgrade. |
| `AiPlayerbot.RandomGearBlacklist` | `` (empty) | `` (empty) | **Gear Exclusion List:** Item IDs never picked by random gear (seed/hire/upgrade). Comma-separated, e.g. `12345,67890`. |
| `AiPlayerbot.AutoEquipUpgradeLoot` | `1` | **`1`** | **Equip Loot Upgrades:** Bots equip upgrades obtained from looting or quests. |
| `AiPlayerbot.AutoPickReward` | `yes` | **`yes`** | **Quest Reward Pick:** Bots pick the first useful quest reward automatically (`no` = list all, `ask` = pick useful and list if multiple). |
| `AiPlayerbot.AutoPickTalents` | `full` | **`full`** | **Auto Talents:** Bots pick talent points based on current spec. |
| `AiPlayerbot.AutoTrainSpells` | `yes` | **`yes`** | **Auto Train:** Bots train all available spells at trainers while they have the money. |
| `AiPlayerbot.XPRate` | `3` | **`3`** | **Bot XP Rate:** Server XP rate × this value for bots. |
| `AiPlayerbot.GlobalCooldown` | `1500` | **`1500`** | **Cast pacing:** Delay between two short-time spell casts. |
| `AiPlayerbot.RandomGearMaxSourceTier` | `0` | **`0`** | **Seed Gear Source Tier:** Highest loot source tier fresh/hired bots may wear — `0` base only (world drop, vendor, quest, allowed recipe), `1` + end-game dungeons (Blackrock Spire, Scholomance, Stratholme, Karazhan Crypt), `2` + raids. Per-item lowest-source-wins, persisted in `ai_playerbot_item_info_cache`. Raise later to unlock tiers (roadmap #289). |
| `AiPlayerbot.RandomGearAllowReputation` | `0` | **`0`** | **Seed Rep Gear:** Allow reputation-gated gear (item/quest/vendor/recipe rep) on fresh/hired bots. |
| `AiPlayerbot.RandomGearAllowPvP` | `0` | **`0`** | **Seed PvP Gear:** Allow PvP gear (honor rank, NO_DISENCHANT rewards) on fresh/hired bots. |
| `AiPlayerbot.RandomGearSeedEpicChance` | `0.02` | **`0.02`** | **Seed World-Epic Chance:** Per-slot chance a fresh seed rolls a rare loot-attested BoE world epic instead of the green/blue band; falls back to the band when the slot has none. |

The spec weights these caches are scored with come from the `ai_playerbot_weightscales` and `ai_playerbot_weightscale_data` tables, seeded by `data/sql/world/20260916090001_world.sql`. If bots wear wrong-slot gear from their bags but never swap an upgrade in, that dataset is empty — re-apply the migration and restart.
---

## 2. World Population & Immersion Flags

These flags control the behavior of autonomous random bots roaming the world:

| Setting | Default | Recommended | What It Does |
| :--- | :---: | :---: | :--- |
| `AiPlayerbot.RandomBotAutologin` | `0` | **`1`** | Automatically logs in random bots (characters on managed pool accounts) at server startup. |
| `AiPlayerbot.RandomBotAutoCreate` | `0` | **`1`** | Automatically creates new bot accounts/characters if the active pool is below `MinRandomBots`. Required by `RandomBotPoolReset`: a reset is refused without it, so the pool is never left empty. |
| `AiPlayerbot.MinRandomBots` / `MaxRandomBots` | `0` | `50` / `150` | Sets the minimum and maximum active random bot population. |
| `AiPlayerbot.RandomBotPoolReset` | `off` | `off` (production) | **Managed pool rebuild:** `off` never resets, `once:<token>` rebuilds the pool on the next server start when `<token>` has not been applied yet, `always` rebuilds on every start (**development only**, destroys all bot progression). Needs `RandomBotAutoCreate = 1`. See [Resetting the managed bot pool](living-world.md#resetting-the-managed-bot-pool). |
| `AiPlayerbot.RandomBotAccountPrefix` | `RNDBOT` | `RNDBOT` | Prefix used to *name* new pool accounts and to list legacy accounts for adoption at the server console. It never authorizes ownership: only accounts registered in `tortoise_bots_pool_account` are managed pool accounts. |
| `AiPlayerbot.RandomBotStartLevelMin` / `Max` | `1` / `60` | `1` / `60` | **Fresh-Bot Level Seed:** A newly created pool bot gets a random level in this range once, on its first login, before its skills, professions and starter gear are seeded, so a fresh pool has bots at every level instead of all walking up from 1. Narrow it for test pools (e.g. `10` / `15`); set both to `1` for the historic level-1 start. |
| `AiPlayerbot.LevelLadder` | `1` | `1` | **Level ladder:** picks which pool bots are online by level band (1-5, 6-10, ... 56-59, plus 60 capped at `LevelLadderMaxLevelShare` percent) instead of round-robin over the whole pool, so the online population stays spread across levels. `0` restores the round-robin. Tuning: `LevelLadderBandSize` (default `5` levels per band), `LevelLadderMaxLevelShare` (default `10` % cap for level 60), `LevelLadderLogMinutes` (default `5`, per-band summary in the log). |
| `AiPlayerbot.RandomBotMaxLevel` | `60` | `40` (test pools) | Caps the gear/item tables random bots roll from; it does not assign bot levels. |
| `AiPlayerbot.minEnchantingBotLevel` | `10` | `10` | **Enchant level gate:** bots at or above this level get level-appropriate permanent enchants from `ai_playerbot_enchant_candidates` (best stat weight for class/spec, tier-0 non-rep sources only). Below it gear stays unenchanted. Hunter scopes use the same path. Set to `61` to disable. |
| `AiPlayerbot.RandomBotInvitePlayer` | `1` | `1` | Random bots in the open world will invite solo human players to form questing groups. |
| `AiPlayerbot.RandomBotGroupNearby` | `1` | `1` | Bots will organically invite each other to form questing parties and dungeon groups. |
| `AiPlayerbot.RandomBotFormGuild` | `1` | `1` | Bots will buy guild charters, collect signatures from other bots, and found their own guilds. |
| `AiPlayerbot.EnableGreet` | `1` | `1` | Bots wave or say hello when passing players in towns and roads. |
| `AiPlayerbot.RandomBotShowHelmet` / `ShowCloak`| `1` | `1` | Renders helmets and cloaks on bots. |
| `AiPlayerbot.RandomBotSayWithoutMaster` | `0` | `0` | Masterless bots say in `/s` what they would whisper to an owner (travel plans, cast failures). `0` keeps them silent unless owned. Needs restart. |
| `AiPlayerbot.AllowIsolatedCustomStartingZones` | `0` | `0` | When 0, blocks random bots from custom isolated starter zones (Blackstone Island, Thalassian Highlands, Alah'Thalas) and normalizes them to mainland starter zones. |
| `AiPlayerbot.HireEnabled` | `1` | `1` | Master switch for on-demand companion hiring (`.bot hire` + `<Mercenary Hire>` inn recruiters). |
| `AiPlayerbot.HireMinAccountSecurity` | `0` | `0` | Minimum account security that may hire (`0` = everyone). |
| `AiPlayerbot.HireMaxBotsPerPlayer` | `4` | `4` (up to `39` for 40-man raids) | Cap on hired companions per player. Party hires stop at 4; raise toward 39 with a raid group. |
| `AiPlayerbot.HireRequiresResting` | `1` | `1` | `.bot hire` requires resting; recruiter gossip skips the check (presence is proof). |
| `AiPlayerbot.HireBaseCostCopper` / `HirePartyMult2/3/4` | `15000` / `1.66` / `2.66` / `4.66` | defaults | Level-scaled party cost curve (~15g total for 4 bots at 60). Set base to `0` for free hiring. |
| `AiPlayerbot.HireRaidFlatCostCopper` | `10000` | `10000` | Flat per-bot rate for raid hires 5+ at 60 (1g). |
| `AiPlayerbot.HireDisconnectGracePeriod` | `300` | `300` | Seconds a hired companion guards after its master disconnects before dismissing. |

---

## 3. Autonomous Services

All autonomous services are fully bounded. LFT autofill and the AH market are disabled by default — enable only the services you need. Battleground auto-queue is on by default:

| Enable with | Default | Description |
| :--- | :---: | :--- |
| `AiPlayerbot.RandomBotLftEnabled = 1` | `0` | **LFT Dungeon Autofill:** When real players queue for Looking-For-Trouble dungeons and wait for missing roles (e.g. Tank or Healer), eligible bots fill the vacant slots and run the instance. |
| `AiPlayerbot.AhMarketEnabled = 1` | `0` | **Living Auction House:** Bots post gathered trade goods and bind-on-equip gear on the Auction House, and bid on/buyout items using real player pricing models. |
| `AiPlayerbot.RandomBotBgEnabled = 1` | `1` | **Battleground Auto-Queue (on by default):** Injects random bots into Warsong Gulch, Arathi Basin, and Alterac Valley when human players queue. Set `0` to opt out. Your own party bots queue with you via Join as Group (WSG/AB; AV is solo-only in the core) and auto-accept the invite regardless of this toggle. |

---

### Bot activity (performance)

| Setting | Default | What It Does |
| :--- | :---: | :--- |
| `AiPlayerbot.DisableActivityPriorities` | `1` | `1`: every bot's AI is always fully active and `botActiveAlone` is ignored. `0`: bots near or visible to a player, in combat, in an instance, grouped with a player or in a battleground stay active; "alone" bots are throttled. All bots stay logged in either way — only their AI update rate changes. |
| `AiPlayerbot.botActiveAlone` | `10` | **Percentage** (not a count) of "alone" bots (no player nearby, empty map/server) kept fully active when priorities are turned on (opt-in, `DisableActivityPriorities = 0`); the active set rotates about 1% per minute. With 20 bots, expect ~2 active when nobody is around. |
| `AiPlayerbot.ForceActiveWhenNearPlayer` | `0` | Also treat bots merely visible to a player as always reacting. |
| `AiPlayerbot.DisableBotOptimizations` | `0` | Currently has **no effect** (read but unused). |

---

## 4. Combat & Reaction Thresholds

Fine-tune how aggressively bots heal, rest, or drink:

| Setting | Default | Tuning Guidance |
| :--- | :---: | :--- |
| `AiPlayerbot.CriticalHealth` | `20` | Percent health considered an emergency. Triggers *Lay on Hands*, *Last Stand*, *Shield Wall*, or *Divine Shield*. |
| `AiPlayerbot.LowHealth` | `50` | Percent health triggering prioritized heavy heals (*Greater Heal*, *Healing Wave*). Increase to `65` for safer dungeon runs. |
| `AiPlayerbot.MediumHealth` | `70` | Threshold for maintenance heals (*Renew*, *Rejuvenation*, *Flash Heal*). |
| `AiPlayerbot.AlmostFullHealth` | `90` | Health ceiling above which bots stop casting heals to conserve mana. |
| `AiPlayerbot.LowMana` | `15` | Mana floor where casters switch to low-cost wanding or conserve mana. |
| `AiPlayerbot.MediumMana` | `40` | Threshold where bots consider conservative rotations. |

---

## 5. In-Game Live Toggles (On the Fly via `/tbm` or Chat)

You do not need to restart the server to adjust tactical behavior during gameplay. You can toggle these anytime:

### Tactical Gameplay Toggles (`.bot action <toggle>`)
- `.bot action aoe <on|off>` — Enables or disables Area-of-Effect abilities (crucial around crowd-controlled mobs).
- `.bot action pull [seconds]` — Tank pulls your target and stays fighting there; party DPS hold at your position for `seconds` (0-60, default `AiPlayerbot.PullDpsJoinDelay = 10`) after the pull lands. The addon advertises support via the `TBM:CAPS|pull-seconds` roster trailer.
- `.bot action pullback [seconds]` — Tank pulls from range and returns to your position, holding there; party DPS join `seconds` (0-60, default `AiPlayerbot.PullBackDpsJoinDelay = 3`) after the tank is back. The return leg is capped by `AiPlayerbot.PullBackMaxReturnTime = 15`.
- `.bot action focus` — Focuses party damage onto the mob marked with the Skull raid marker.
- `.bot action cc <mark>` — Assigns CC to a specific raid marker (e.g. Moon, Star).

### Strategy & Stance Toggles
Using `.bot strategy <change> [Name]` (change first, optional bot name second) or the `/tbm` addon:
- `.bot strategy +conserve mana` / `.bot strategy -conserve mana` — Restricts casters to basic, high-efficiency spells.
- `.bot strategy +loot` / `.bot strategy -loot` — Toggles whether bots run to loot corpses after combat.
- `.bot strategy +silent` / `.bot strategy -silent` — Silences bot chatter in party/say chat so they execute commands quietly.
- `.bot strategy -passive` — Halts all bot attacks; bots will only follow and hold fire.
- `.bot formation <arrow|line|circle|shield>` — Changes follow positioning around the leader.

---

## 6. Diagnostic Logging

The native module layer (bot lifecycle, random-bot, Auction House, battleground queue, and LFT services — everything under `host/` and `runtime/`) has its own verbosity setting, independent of the core server's own `LogLevel`. This lets you trace what the module is doing without turning on the engine's full debug output, and vice versa.

```ini
[TortoiseBotsConf]
TortoiseBots.LogLevel = 2
```

| Level | Name | Shows |
| :---: | :--- | :--- |
| `0` | Minimal | Errors only. |
| `1` | Basic | One-off startup, shutdown, and diagnostic test results (e.g. `PendingAddRemoveTest`, `AutoTest`). |
| `2` | Detail (default) | Per-bot state transitions: session start/stop, add/remove, AH postings, BG queue entries. |
| `3` | Debug | Per-tick and per-packet traces. High volume — intended for short diagnostic sessions, not left on. |

Errors (`sLog.outError`) are always written regardless of this setting. The level is re-read on `.reload config`, so it can be raised or lowered without a server restart.

This setting is separate from the strategy AI's own action trace, which stays gated behind the `debug`/`debug action` bot strategies (`.bot strategy +debug`) rather than a server-wide config key.

---

## 7. Bot Chatter & Broadcasts

Flavor and status lines come from the `ai_playerbot_texts` table (seeded by
`data/sql/world/20260913090000_world.sql`). If bots speak raw keys such as
`quest_accepted`, that table is empty — re-apply the migration and restart.
Volume is controlled by these knobs (all need a restart):

| Setting | Default | What It Does |
| :--- | :---: | :--- |
| `AiPlayerbot.EnableBroadcasts` | `1` | Master switch. `0` disables all quest/loot/kill/level-up/suggest broadcasts. |
| `AiPlayerbot.BroadcastToWorldGlobalChance` / `BroadcastToGeneralGlobalChance` | `3000` | Main throttle on what reaches world/general chat (range `0`-`30000`). `0` re-routes most broadcasts away from that channel. |
| `AiPlayerbot.BroadcastChance*` | varies | Per-event chance, e.g. `BroadcastChanceQuestAccepted`, `BroadcastChanceSuggestSell`. `0` disables that one class. The toxic lines (`*Toxic*`) default to `0`; the Thunderfury joke defaults to `1` — set `AiPlayerbot.BroadcastChanceSuggestThunderfury = 0` to silence it. |

## 8. Settings that currently have no effect

These keys are read into the config object (so old config files keep
loading) but nothing consumes them. They are marked in
`aiplayerbot.conf.dist.in` with "Currently has no effect (read but unused)."
and listed here so nobody tunes a dead knob:

`AhMarketValueVendor`, `AllowGuildBots`, `AllowMultiAccountAltBots`,
`BotAutologin`, `DiffEmpty`, `DiffWithPlayer`, `DisableBotOptimizations`,
`FreeMoveDelay`, `GroupMemberLootDistanceWithActiveMaster`,
`InstantRandomize`, `LLMApiKey`, `MaxFreeMoveDistance`,
`MinRandomBotInWorldTime`, `MinRandomBotsPriceChangeInterval`,
`RandomBotRandomPassword`, `RandomBotTimedOffline`,
`RandomGearTabardsUnobtainable`, `RandombotsWalkingRPG.InDoors`,
`RespawnModNeutral`, `RespawnModHostile`, `RespawnModThreshold`,
`RespawnModMax`, `RespawnModForPlayerBots`, `RespawnModForInstances`,
`TweakValue`.
