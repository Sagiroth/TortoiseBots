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

Configuration lives in two installed files. The first install creates them from their templates (installed beside them as `.dist`); later installs never overwrite them, so compare with the `.dist` after an update to pick up new keys (a missing key uses its default):
1. `aiplayerbot.conf`, installed next to `mangosd.conf` — every `AiPlayerbot.*` gameplay flag, QoL toggle, AI threshold and service. Template: `ai/playerbot/aiplayerbot.conf.dist.in`. A different path can be set with `AiPlayerbot.ConfigFile` in `mangosd.conf`.
2. `modules/tortoise_bots.conf` in the server config directory — `TortoiseBots.*` module options (log level, telemetry). Template: `conf/tortoise_bots.conf.dist`.

Both are read once at server start; `.reload config` only re-applies `TortoiseBots.LogLevel`. A line that is commented out (`#`) in the template sets nothing — the code default listed below applies.

---

## 1. Player Quality-of-Life (QoL) Flags

These settings dramatically enhance the solo or small-group experience with owned bots:

| Setting | Default | Recommended | What It Does |
| :--- | :---: | :---: | :--- |
| `AiPlayerbot.SyncAltLevelToMaster` | `0` | **`1`** | **Auto-Level Bot Alts:** While an owned (non-random) bot is grouped with you and you are the group leader, it gains one level per AI update until it matches your level. |
| `AiPlayerbot.BoostFollow` | `0` | **`1`** | **Mount Up to Catch Up:** Bots far behind the leader (beyond react distance) trigger a mount check so they ride to catch up instead of trailing on foot. No speed hack; combat-safe. |
| `AiPlayerbot.DefaultFormation` | `chaos` | `chaos` | **Idle follow spread:** idle party bots hold a loose ring around the leader with a small re-rolled offset (mod-playerbots parity) instead of stacking on one spot. `.bot formation` still overrides per bot. |
| `AiPlayerbot.NonGmFreeSummon` | `0` | **`1`** | **Free summon action:** lets non-GM players teleport a bot straight to them with the in-chat summon order (the `summon` action). Without it the bot walks or uses a meeting stone or innkeeper instead. Does not affect `.bot summon <Name>`, which always works on your own and hired bots. |
| `AiPlayerbot.OwnedBotMaintenanceEnabled` | `0` | `0` | **Owned-bot maintenance (issue #473):** enables `.bot maintenance <Name>` for owned bots and hired companions (never pool bots). Off by default; with it off the command replies that the feature is disabled. |
| `AiPlayerbot.OwnedBotAutogearEnabled` | `0` | `0` | **Owned-bot autogear (issue #473):** enables `.bot autogear <Name> [quality\|ilvl]` for owned bots and hired companions (never pool bots). Incremental re-gear within the caps below; off by default. |
| `AiPlayerbot.OwnedBotAutogearQualityCap` | `2` | `2` | **Autogear quality cap:** 0 poor .. 5 legendary (default 2 = green). Per-command quality words are clamped to it. |
| `AiPlayerbot.OwnedBotAutogearIlvlCap` | `0` | `0` | **Autogear item-level cap:** 0 = no cap; otherwise both the per-command ilvl and the gear candidate pool are clamped to it. |
| `AiPlayerbot.OwnedBotSummonWhenGroup` | `0` | `0` | **Summon on group accept (issue #473):** an owned bot (or hired companion) that accepts your group invite is summoned to you when far away (beyond sight distance, same map). Never pool bots; off by default. |
| `AiPlayerbot.OwnedBotSummonAllowInCombat` | `0` | `0` | Summon-on-accept may fire while either side is in combat. |
| `AiPlayerbot.OwnedBotSummonAllowMasterDead` | `0` | `0` | Summon-on-accept may fire while the inviter is dead. |
| `AiPlayerbot.OwnedBotSummonAllowBotDead` | `0` | `0` | Summon-on-accept may fire while the bot is dead (revive needs `OwnedBotSummonRevive` too). |
| `AiPlayerbot.OwnedBotSummonRevive` | `0` | `0` | Revive a dead bot as part of summon-on-accept (needs the dead-bot knob too). |
| `AiPlayerbot.OwnedBotSummonRepair` | `0` | `0` | Repair the bot after a summon-on-accept arrival (out of combat unless `OwnedBotSummonAllowInCombat` is also on). |
| `AiPlayerbot.OwnedBotSummonCooldown` | `300` | `300` | Seconds between summon-on-accept teleports per bot (5 min). An uninvite/invite cycle inside the window is refused, so it cannot be macroed into a free in-combat rez/repair. `0` disables the cooldown. |
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
| `AiPlayerbot.RandomBotStallRelogMinutes` | `45` | `45` | A pool bot below level 60 that earns no XP for this many minutes is relogged. About 5% of a fresh pool went idle after an early level-up and only a relog brought them back. `0` = off. |
| `AiPlayerbot.RandomBotMaxLevel` | `60` | `40` (test pools) | Caps the gear/item tables random bots roll from; it does not assign bot levels. |
| `AiPlayerbot.minEnchantingBotLevel` | `10` | `10` | **Enchant level gate:** bots at or above this level get level-appropriate permanent enchants from `ai_playerbot_enchant_candidates` (best stat weight for class/spec, tier-0 non-rep sources only). Below it gear stays unenchanted. Hunter scopes use the same path. Set to `61` to disable. |
| `AiPlayerbot.RandomBotInvitePlayer` | `1` | `1` | Random bots in the open world will invite solo human players to form questing groups. |
| `AiPlayerbot.RandomBotGroupNearby` | `0` | `0` | Pool bots invite each other to form questing parties and dungeon groups. Off by default (mod-playerbots parity): bot-only groups leash members via follow and idle the followers. Pool-bot skips (invite) and declines (accept) apply only when this is `0`; inviting real players is governed separately by `RandomBotInvitePlayer`. Set `1` to restore organic bot grouping. |
| `AiPlayerbot.InviteChat` | `1` | `1` | Bots announce group/raid invites in say or guild chat. Only a real player is announced to — a bot announcing an invite to another bot is skipped, because bots invite each other non-stop while a pool fills up. |
| `AiPlayerbot.RandomBotFormGuild` | `1` | `1` | Bots will buy guild charters, collect signatures from other bots, and found their own guilds. |
| `AiPlayerbot.EnableGreet` | `1` | `1` | Master switch for greetings: a bot greets a real player passing by with a text emote, at most once per encounter. Bots never greet other bots — a pool packs hundreds of them into one zone, so a greeting per bot meeting is chat no human reads. `0` disables greetings entirely. |
| `AiPlayerbot.GreetCooldown` | `600` | `600` | Seconds before the same real player may be greeted again by the same bot. A player who keeps walking in and out of sight is greeted once per window, not on every pass. |
| `AiPlayerbot.RandomBotGreet` | `0` | `0` | Masterless pool bots also greet real players. `0` keeps greetings to bots with a live player master: hundreds of pool bots sharing a starting zone would otherwise answer every visitor in a wave. Set `1` for the old open-world greeting from the pool. |
| `AiPlayerbot.RandomBotShowHelmet` / `ShowCloak`| `1` | `1` | Renders helmets and cloaks on bots. |
| `AiPlayerbot.RandomBotSayWithoutMaster` | `0` | `0` | Masterless bots say in `/s` what they would whisper to an owner (travel plans, cast failures). `0` keeps them silent unless owned. Needs restart. |
| `AiPlayerbot.PoolBotTradeMode` | `0` | `0` | **Pool-bot trading (issue #469, mod-playerbots `EnableRandomBotTrading` parity):** `0` off (a masterless pool bot refuses a stranger's trade window), `1` trusted (its master, group members, or its hire master only), `2` buy-only (takes, never gives), `3` sell-only (gives, never takes), `4` on (unrestricted). Owned/hired bots always trade with their master. Out-of-range values fail closed to `0`. Bot-to-bot trade life (RPG giveaways, enchants, `WTS`/`WTB` chatter) is untouched — the gate applies only to a real-player stranger trading a pool bot. |
| `AiPlayerbot.TradeActionExcludedPrefixes` | `RPLL_H_,DBMv4` | `RPLL_H_,DBMv4` | Comma-separated chat prefixes that never drive the trade action (donor parity): addon chatter such as Questie/DBM/RPLL lines cannot open a trade window, even when the bot is named. Only the Trade channel is mention-gated (General/LFG/Defense/World unchanged): Trade chatter without a direct mention is ignored unless the bot is addressed. |
| `AiPlayerbot.TurtleMountAtLevel` | `18` | `18` | Random bots at this level..39 learn the Swift Riding Turtle (quest 40302 "Torta's Egg" -> item 23720 -> spell 30174) on seed/refresh and masterless level-up, as if they did the quest. `0` disables. Level 40+ behaviour is unchanged (normal mount + riding, epic at 60). |
| `AiPlayerbot.LevelUpMounts` | `1` | `1` | Organic level-up mounts: a random pool bot that dings 40 or 60 learns the same mount the seed/hire field kit grants (slow mount at 40, epic at 60; the riding skill is level-gated by `InitSkills` on every level-up already). Idempotent per tier, so seed/re-seed and a repeated level-up packet never stack mounts. `0` = off: a bot levelled from 1 keeps only the turtle until it is re-seeded. |
| `AiPlayerbot.MountBreakEvenFactor` | `1.5` | `1.5` | Safety factor on the mount break-even distance: bots mount for travel/RPG trips only when the remaining distance exceeds `7 y/s * cast time / (best speed % / 100) * this` (turtle 9% -> ~350 y, 60% -> ~50 y, 100% -> ~30 y). `0` disables the threshold (mount for any trip). |
| `AiPlayerbot.RepopAtGraveyard` | `1` | `1` | Random bots without a player master repop at the nearest graveyard instead of their racial starting area. |
| `AiPlayerbot.AvoidHostileTowns` | `1` | `1` | Random bots without a player master refuse travel destinations and grind targets guarded by town guards hostile to their team; guards of the bot's own team are ignored. Neutral-town bruisers (Booty Bay, Gadgetzan, Ratchet, Everlook) only stop the bot from starting fights there; travel routes and destinations may pass through those towns. |
| `AiPlayerbot.UnstuckHearthLevelFit` | `1` | `1` | A long-stuck random bot without a player master hearths only if its homebind area is within 10 levels of it; otherwise it repops. `0` skips the level check; the distance gate below still applies. |
| `AiPlayerbot.UnstuckHearthMinDistance` | `300` | `300` | Yards from its own homebind a long-stuck random bot must be before a hearthstone is worth casting; closer than this the unstuck chain repops instead, so the bot does not burn its 30-minute hearthstone cooldown to land a few hundred yards away (a level 1 starting zone is that small). `0` always hearths. |

| `AiPlayerbot.BotQuestLogUpkeep` | `1` | `1` | Masterless random bots drop finished quests they can no longer reward (except money blockers) and grey itemless deliver quests, skip grey quests unless the reward is an equip upgrade, refuse war-effort item turn-ins (AQ materials, signets) and banned quests (CLUCK!, inactive templates) at accept, clean a nearly-full log only when something is droppable (failed quests and unfinishable solo picks — over-level, elite/dungeon/raid, group-suggested, wrong zone — drop when fewer than two slots are free), and stay in turn-in travel from the first finished quest until none remain. `0` restores the old behaviour. Owned/alt bots unaffected (banned quests excepted — they drop everywhere). |
| `AiPlayerbot.LeaveOutgrownZones` | `1` | `1` | A random masterless bot at level 10+ in a non-capital zone it outlevels by 5+ — or idling in a capital with no pending trainer/mount/AH/Vendor/Repair need — walks to a level-band Grind hub outside the zone it is leaving (capital-idle leaves keep every zone), skips services and inn homebinds in zones 10+ below it (capitals excepted). Unknown area levels fail closed. `LeaveOutgrownZone`/`TravelTarget` rows in `bot_events.csv` count fires. |
| `AiPlayerbot.AllowIsolatedCustomStartingZones` | `0` | `0` | When 0, blocks random bots from custom isolated starter zones (Blackstone Island, Thalassian Highlands, Alah'Thalas) and normalizes them to mainland starter zones. |
| `AiPlayerbot.RandomBotEvenStartZones` | `1` | `1` | Spreads new random bots evenly across the six racial starting zones: auto-create picks a valid race from the least-populated start zone (counted once per creation batch, level-1 pool characters) and isolated-custom-zone normalization sends the bot to the least-populated standard start of its faction (core `playercreateinfo` spawns). `0` keeps the old behaviour: uniform random race, goblins to Valley of Trials, high elves to Northshire. |
| `AiPlayerbot.VendorBatchMinCount` / `VendorBatchMinBagSpace` | `8` / `60` | `8` / `60` | Vendor trip batch rule (#339) for bots at level 5+: a bot walks to a vendor once its vendor-usable stock holds at least this many items while the bags are at least this percent full, or covers the money missing for its next trainable class rank. The batch half stands on its own — a bot with nothing left to train (a fresh level 1 has no green class rank yet) still sells its loot instead of hoarding it. |
| `AiPlayerbot.LowLevelVendorBatchMinCount` / `LowLevelVendorBatchMinBagSpace` | `3` / `25` | `3` / `25` | Same batch rule for bots below level 5, which carry one or two greys: `8` items / `60`% bags never fired for a starting pool, so it never sold anything. |
| `AiPlayerbot.LowLevelVendorMaxDistance` | `600` | `600` | Yards. All RPG travel stays blocked below level 5 (the path to any NPC usually crosses level 5+ mobs); a random masterless bot below level 5 may still reach its own camp vendor, capped to this radius and to the starting-zone level band (area level <= bot level + 5). `0` disables beginner vendor trips. The vendor travel purpose searches without the RPG destination pre-filter (the same way the named trainer errand does), so this radius is enforced at the partition gate; `Vendor` travel choices and `SellAction` rows in `bot_events.csv` count the effect, and `SellErrandFailed` (an errand sale that sold nothing, with the reason) and `VendorTripNoTarget` (a `Vendor` destination search that came up empty) … |
| `AiPlayerbot.BotLootRateUncommon` / `BotLootRateRare` | `1.0` / `1.0` | owner-tuned | Bot-only drop-rate boost for green (uncommon) and blue (rare) items. Masterless random pool bots roll each quality-2/3 entry of a creature's loot template one extra time per kill at `(multiplier - 1) x` the entry's DB chance, gated by the chance of every reference row on the path, so the expected greens/blues per kill are roughly `multiplier x` the normal rate. Quest-only drops and quest starters are never boosted, unique items already held at `max_count` are skipped, and entries of other qualities are untouched (a creature that never drops greens gains nothing). Players and hired/alt bots keep the server's own rates, and loot rolled for a group is never touched. Each hit logs a `BotLootBonus` row (creature name, `quality:itemId:itemName`) to `bot_events.csv`. `1.0` = off. |
| `AiPlayerbot.HireEnabled` | `1` | `1` | Master switch for on-demand companion hiring (`.bot hire` + `<Mercenary Hire>` inn recruiters). |
| `AiPlayerbot.HireMinAccountSecurity` | `0` | `0` | Minimum account security that may hire (`0` = everyone). |
| `AiPlayerbot.HireMaxBotsPerPlayer` | `4` | `4` (up to `39` for 40-man raids) | Cap on hired companions per player. Party hires stop at 4; raise toward 39 with a raid group. |
| `AiPlayerbot.HireAnywhere` | `0` | `0` | `0`: companions are hired only at a `<Mercenary Hire>` recruiter; `1`: `.bot hire` also works anywhere. Fees are the same either way. |
| `AiPlayerbot.HireBaseCostCopper` / `HirePartyMult2/3/4` | `15000` / `1.66` / `2.66` / `4.66` | defaults | Level-scaled party cost curve (~15g total for 4 bots at 60). Set base to `0` for free hiring. |
| `AiPlayerbot.HireRaidFlatCostCopper` | `10000` | `10000` | Flat per-bot rate for raid hires 5+ at 60 (1g). |
| `AiPlayerbot.HireDisconnectGracePeriod` | `300` | `300` | Seconds a hired companion guards after its master disconnects before dismissing. |
| `AiPlayerbot.WorldBuffsEnabled` | `1` | `1` | Master switch for world buffs at the six capital `<Mercenary Hire>` recruiters (`World buffs` branch, purchases, unlock credit). Unlock quests stay visible in the DB while off (core reads the gossip `!` straight from the relations); without the branch no quest can be taken or completed. |
| `AiPlayerbot.WorldBuffsKeepInRaids` | `0` | `0` | `1`: raid entrances keep world buffs for everyone (restored with remaining time). `0`: core strips them as usual, and the module additionally strips them in Upper Karazhan (814). |
| `AiPlayerbot.WorldBuffsMinLevel` | `60` | `60` | Minimum level that sees the `World buffs` branch. |
| `AiPlayerbot.WorldBuffsPriceBaseCopper` / `WorldBuffsPricePerPersonCopper` | `100000` / `20000` | defaults | Purchase price pair (base + per-person x head count) for Rallying Cry, Warchief's Blessing, Spirit of Zandalar and the DM tribute pack (10g + 2g/person). |
| `AiPlayerbot.WorldBuffsSaygePriceBaseCopper` / `WorldBuffsSaygePricePerPersonCopper` | `60000` / `10000` | defaults | Same pair for Sayge's Dark Fortune (6g + 1g/person). |
| `AiPlayerbot.WorldBuffsSongflowerPriceBaseCopper` / `WorldBuffsSongflowerPricePerPersonCopper` | `40000` / `10000` | defaults | Same pair for Songflower Serenade (4g + 1g/person). |
| `AiPlayerbot.WorldBuffsSilithystPriceBaseCopper` / `WorldBuffsSilithystPricePerPersonCopper` | `20000` / `4000` | defaults | Same pair for Traces of Silithyst (2g + 40s/person). |
| `AiPlayerbot.WorldBuffsUnlockFeeRaidCopper` / `WorldBuffsUnlockFeeCopper` | `2000000` / `1000000` | defaults | Turn-in fee of the unlock quests: Rallying Cry, Warchief's Blessing and Spirit of Zandalar (200g) / the rest (100g). `0` = free. The server writes the fee into the quests at startup, so a change takes effect after the **next** restart (restart twice). |

---

## 3. Autonomous Services

All autonomous services are fully bounded. LFT autofill and battleground auto-queue are on by default (both only act while a real player is queued); the AH market stays off unless enabled:

| Enable with | Default | Description |
| :--- | :---: | :--- |
| `AiPlayerbot.RandomBotLftEnabled = 1` | `1` | **LFT Dungeon Autofill (on by default; set `0` to opt out):** When real players queue for Looking-For-Trouble dungeons and wait for missing roles (e.g. Tank or Healer), eligible bots fill the vacant slots. Fill is demand-driven only — bots never queue without a waiting human. Your own party bots queue with you and auto-accept the dungeon offer (works with the switch on or off); after the group forms the party walks to the portal together, so `.bot summon` stragglers — there is no automatic teleport. |
| `AiPlayerbot.AhMarketEnabled = 1` | `0` | **Living Auction House:** Bots post gathered trade goods and bind-on-equip gear on the Auction House, and bid on/buyout items using real player pricing models. |
| `AiPlayerbot.RandomBotBgAutonomous = 1` | `1` | **Autonomous Bot-Only Matches (on by default; set `0` to opt out):** Pool bots start their own Warsong Gulch (10v10, most populated ready bracket, max 1 concurrent instance per bracket) when no human is queued. Batches accumulate each tick until both sides fill; once the match starts forming, top-up stops. Requires `AiPlayerbot.RandomBotBgEnabled = 1`. |
| `AiPlayerbot.RandomBotBgEnabled = 1` | `1` | **Battleground Auto-Queue (on by default):** Injects random bots into Warsong Gulch, Arathi Basin, and Alterac Valley when human players queue. Set `0` to opt out. Your own party bots queue with you via Join as Group (WSG/AB; AV is solo-only in the core) and auto-accept the invite regardless of this toggle. |

---

### Bot activity (performance)

| Setting | Default | What It Does |
| :--- | :---: | :--- |
| `AiPlayerbot.DisableActivityPriorities` | `1` | `1`: every bot's AI is always fully active and `botActiveAlone` is ignored. `0`: bots near or visible to a player, in combat, in an instance, grouped with a player or in a battleground stay active; "alone" bots are throttled. All bots stay logged in either way — only their AI update rate changes. |
| `AiPlayerbot.botActiveAlone` | `10` | **Percentage** (not a count) of "alone" bots (no player nearby, empty map/server) kept fully active when priorities are turned on (opt-in, `DisableActivityPriorities = 0`); the active set rotates about 1% per minute. With 20 bots, expect ~2 active when nobody is around. |
| `AiPlayerbot.ForceActiveWhenNearPlayer` | `0` | Also treat bots merely visible to a player as always reacting. |
| `AiPlayerbot.DisableBotOptimizations` | `0` | Currently has **no effect** (read but unused). |

### Per-tick AI pass budget (large pools)

The module's AI pass runs at the end of every world tick, so a bot a real player is playing with used to queue behind the whole random pool. Player-owned bots now always update first, every tick, and are never throttled; the random pool runs after them in a round-robin order that resumes where the previous tick stopped, so a bot that misses a pass is served on a later one and never dropped.

**Who counts as player-owned** (the rule is deliberately about real ownership, not bookkeeping flags):

- a bot whose master is a **live player with a network session** — hired companions and party bots while their player is online; and
- a bot on an account the random pool does **not** own (`tortoise_bots_pool_account`), i.e. the owner's own characters.

Everything else is the pool, including characters on `RNDBOT` pool accounts that carry a master from a bot-only group, a stale `PlayerMaster` lease, or `random = false` after a restart. Those flags are not ownership signals: bot-only groups hand their members a bot master and a lease (`PlayerbotAI::GetGroupMaster` → `BotManager::BindBotMaster`), which once misclassified ~40 % of a 500-bot pool (212 bots) as player-owned and kept them unbudgeted.

The pool is only throttled when the server is already struggling. On an average PC with a small pool the ticks are short, both keys stay out of the way, and the pool gets exactly the pass it always got.

| Setting | Default | What It Does |
| :--- | :---: | :--- |
| `AiPlayerbot.PoolTickBudgetUs` | `10000` | Microseconds of module work per tick for the pool pass, once the gate below opens. `0` removes the budget entirely (the pool always runs its full pass). Values below `1000` are raised to `1000`; `100000` is the ceiling. |
| `AiPlayerbot.PoolBudgetWhenTickOverMs` | `150` | The gate: the budget applies only while the previous world tick took longer than this. `0` applies it on every tick. A tick at or below the value keeps the unbudgeted, full pool pass. |

Tuning: raise `PoolTickBudgetUs` (e.g. `25000`–`50000`) if pool bots feel sluggish while the tick is long; lower it if player-owned bots still lag. The budget is checked between bots, so a pass can overshoot by one bot's work. With 500 bots on a slow machine every choice means *someone* waits — the gate only decides whether it is the player's party or the pool.

With AI enabled (`AiPlayerbot.Enabled = 1`), the module reports the pass once per ~30 s of world-tick time at `TortoiseBots.LogLevel = 1` or higher (default `2`):

```
TortoiseBots: BOTPERF passUs=812 playerBots=5 ownedBots=5 masterBots=0 poolBots=495 poolProcessed=4 budgetHit=1 maxUs=9820 ticks=62
```

`passUs` is the average `UpdateBots` cost in the window in microseconds (`maxUs` the worst), `playerBots` the unbudgeted candidate count, split into `ownedBots` (owner's account) and `masterBots` (live player master), `poolBots` the pool candidate count, `poolProcessed` the pool slots the rotation advanced past (a record that was unusable that tick still counts), and `budgetHit` `1` when the budget cut that pass short. Read it like this: `budgetHit=1` with a small `passUs` is the throttle working; `budgetHit=1` and `poolProcessed=1` means the budget is too tight for the pool size (raise it); a low `passUs` while ticks are still multi-second says the module pass is not what stretches the tick. A `playerBots` far above the real player's party (check `ownedBots` + `masterBots`) means a classification bug, not a busy player.

### Server settings for many bots (`mangosd.conf`)

Hundreds of always-active bots keep most of both continents busy, which the core's defaults don't expect. Three core settings matter most:

| Setting | Recommended | Why |
| :--- | :---: | :--- |
| `CleanupTerrain` | `0` | The core frees unused terrain every 60 s; bots re-enter it seconds later and the reload stalls the map tick. With 500 bots, slow (>200 ms) map ticks dropped from 126 to 20 per 10 minutes. Memory stays bounded by the continents' terrain (~2 GB extracted). Keep `1` on low-RAM machines. |
| `PlayerSave.Interval` | `300000` | The 60 s default saves every bot in the same minute; 5 min removes that write wave. A crash loses at most this much progress. |
| `DynamicRespawn.MaxReductionRate` / `DynamicRespawn.MinRespawnTime` | `0.75` / `15` | Speed up respawns where many players/bots crowd: the core shortens spawn timers near groups of players (defaults cap the cut at `0.25` and never below `25` s). On bot-heavy servers raise the cap and lower the floor so starter valleys and grind fields keep up with a crowded pool. Core defaults are `0.25` / `25`; the rest of the `DynamicRespawn.*` block stays at its defaults. |

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
- `.bot action pullback [seconds]` — Tank pulls from range and returns to your position, holding there and picking the pulled mob up as it arrives; party DPS join `seconds` (0-60, default `AiPlayerbot.PullBackDpsJoinDelay = 5`) after the tank is back. The return leg is capped by `AiPlayerbot.PullBackMaxReturnTime = 15`.
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
| `1` | Basic | One-off startup, shutdown, and diagnostic test results (e.g. `PendingAddRemoveTest`, `AutoTest`), plus the periodic `BOTPERF` AI-pass cost line. |
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
| `AiPlayerbot.BroadcastChance*` | varies | Per-event chance, e.g. `BroadcastChanceQuestAccepted`. `0` disables that one class. The toxic lines (`*Toxic*`) and the trade spam (`BroadcastChanceSuggestSell`, the "WTS"/"I am selling" lines pool bots hawk in say/yell/trade/general) default to `0`; the Thunderfury joke defaults to `1` — set `AiPlayerbot.BroadcastChanceSuggestThunderfury = 0` to silence it. |

Travel picks ("Traveling 123y to ...") and vendor confirmations ("Selling ...") are silent unless a live player asked: a direct `.bot`/`go`/`where` command reply still answers its requester, but autonomous pool-bot narration and bot-to-bot trade/enchant lines (RPG "You can use this ..." / "Let me enchant this ..." between two bots) never reach chat.

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

### Previous-run bot logs

Enabled logs opened in write mode (including `bot_events.csv` and startup
travel/pathfinding dumps) retain the previous file as `<filename>.1`. Rotation
happens once per filename per process; later dump refreshes leave that backup
alone. Append-only logs retain their existing behavior. No CSV columns change.
If rotation fails, the truncating open is refused and an error is logged; later
ordinary log writes may append to the preserved current file. Disabled logs
are not touched. Keep copies elsewhere if more than one previous run is needed.
