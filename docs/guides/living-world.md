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

TortoiseBots is not limited to player-owned companions. It includes a complete **Living World** subsystem that populates your realm with autonomous bots on **managed pool accounts**. These bots explore zones, grind mobs, gather profession nodes, quest, trade on the Auction House, form parties, and create guilds—making the world feel vibrant and active like a populated MMO server.

> **What "managed" means.** A pool account is one recorded in the character-database table `tortoise_bots_pool_account`: either the module created it itself (auto-create or hiring) or an administrator adopted it explicitly at the server console. A username that merely starts with the configured prefix (`RNDBOT`) does **not** make an account a pool account — such accounts are ignored by the pool and are never reset. See [Resetting the managed bot pool](#resetting-the-managed-bot-pool).

> **Hired companions are guests, not residents.** Players hire companions through the `<Mercenary Hire>` recruiters (see [Player Controls](player-controls.md)). Each hire creates its **own new character** on a managed pool account, and that character is **deleted** when the hire ends — when the companion is kicked or released, when the party disbands, or when the disconnect grace period after the player logs out expires. A dismissed companion never returns to the roaming pool, so it can never drag a player-level character with master-level gear into the organic world. The freed account slot is simply refilled by the normal auto-create target (it never raises or lowers `AiPlayerbot.MinRandomBots`/`MaxRandomBots`), and because the character is gone, the player pays for a fresh companion the next time.

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
| **Off-hand & Shields** | A shield a bot can wear is not thrown away just because the main hand holds a two-hander (the off-hand is unusable then, and the vendor would previously sell every such shield — they ended up in the buyback window). It is kept only for a bot that can come back to a one-hander and shield (protection specs, or a tank role) and only the single best shield it owns; spares are sold as before. The equipment audit puts the kept shield on the moment a one-hander takes the main hand, a fresh seed moves a leftover shield to the bags instead of destroying it, and a protection warrior or paladin that still wields a two-hander swaps to the best one-hander it carries before taking the shield. |
| **Beginner Grinding (Level 1–4)** | Fresh bots in starter valleys (Valley of Trials, Camp Narache, Northshire, Coldridge, Shadowglen) target mobs up to their own level and are permitted to hunt coinless starter beasts (e.g. Mottled Boars, Scorpids, Plainstriders) while ignoring critters. The order cap follows the character, not the zone: below level 10 a bot picks grind targets at most **one** level above itself (quest objectives excepted), so a fresh bot no longer spends its orders on mobs two to four levels up that it almost never kills. |
| **Questing & Progression** | Bots pick up level-appropriate quests from quest givers, track quest objectives (killing specific mobs or collecting items), and turn them in for XP and gold rewards. |
| **Quest-log upkeep (masterless random bots)** | Finished quests that can no longer be rewarded (except transient money blockers) are dropped instead of pinning a log slot, as are grey incomplete deliver quests whose items are gone; grey quests are skipped unless the reward is an equip upgrade; a nearly-full log is cleaned on a timer only when something is droppable; turn-in travel starts at the first finished quest and stays on until none remain, and a bot on that trip walks to the taker instead of starting new fights on the way (a mob that engages it still gets the full combat rotation — only the opening strike is given up, because attacking stops the walk). Owned and alt bots are unaffected. Toggle: `AiPlayerbot.BotQuestLogUpkeep` (default `1`). |
| **Gathering & Professions** | Bots with Herbalism, Mining, or Skinning actively travel to resources and hunt skinning targets to gather materials for crafting and the Auction House. |
| **Town Life & Immersion** | In towns, bots visit class trainers to learn new spell ranks, repair yellow/red durability gear at blacksmiths, vendor junk items, and wave or say hello when passing human players. |

### Persistent Bot Initial Skills (`DisableRandomLevels = 1`)

For servers configured to run persistent, organically leveling bots starting at level 1 (`AiPlayerbot.DisableRandomLevels = 1`):
* **Weapon Skills:** Bots receive full class-compatible weapon proficiencies on their first login, scaled to their current level cap (e.g., 5/5 at level 1). This ensures melee and ranged attacks connect reliably instead of missing 80% of the time at 1/5 weapon skill.
* **Ranged Shoot Abilities:** a weapon skill also teaches that weapon's *shoot* ability (*Shoot Bow*, *Shoot Gun*, *Shoot Crossbow*, *Throw*, wand *Shoot*, hunter *Auto Shot*). Bots get them from the same skill-reward table the game itself uses (`skill_line_ability`), whenever the factory seeds skills (`MakeComplete`, `ProvisionSpellsAndGear`) and once per bot login for characters created before it. Without the ability a bot with a bow, arrows and the Bows skill cannot shoot at all: the AI resolves a spell name only against the bot's own spellbook, so the ranged pull and the *shoot* fallback would silently do nothing. Wand *Shoot* carries the same effect as melee strike abilities (`WEAPON_DAMAGE_NOSCHOOL`), so it is only learned for the weapon-skill line that teaches it (*Wands*), never for a class skill like *Arms*.
* **Dual Wield class level:** Dual Wield is the one ability the data cannot gate for bots — skill 118's `SkillRaceClassInfo` row for warrior/hunter is overridden to `MinLevel 1` by `skill_race_class_info_mod` id 132 ("Show Dual Wield on trainers at all levels"), so nothing keeps a level-5 bot from ending up with skill 118 and spell 674; a rogue row is untouched and says 10. The factory therefore applies explicit thresholds, taken from the live trainer data (`npc_trainer` for the teaching spell 1424: warrior 10 in 29 trainers, hunter 20 in 39) and the owner's call for the rogue (10, even though its 22 trainers say 20): **warrior 10, rogue 10, hunter 20**. Below its threshold a bot is never taught the Dual Wield spell, and at every login the factory strips what it already carries — spell 674, skill 118 and the session flag — while leaving equipment alone: `Player::SetCanDualWield` is a plain flag, nothing in the core unequips the off-hand when it goes false, and an off-hand weapon simply stops contributing until the bot earns the ability (shields are unaffected either way, they never needed the flag). At or above the threshold everything works as described above. Classes that cannot dual wield in this game (shamans get it only in TBC) never get it either: the earlier hard-coded level-10 grant for shamans is gone, and a shaman that still carries the spell or skill loses both at login.
* **Dual Wield, Block and weapon abilities:** the same pass learns every other ability a skill the bot owns teaches, because the core gates those abilities on the *spell*, not on the skill: *Dual Wield* (spell 674) is what lets the core put a second weapon in the off-hand (a bot with skill 118 and a spare dagger in its bag stayed main-hand-only) and *Block* (spell 107) is what makes a shield block. The per-session flags those passives carry (*m_canDualWield*, *m_canParry*, *m_canBlock* are never saved) are restored on every pass from the abilities the bot knows, *Parry* included — its `skill_line_ability` row has `learnOnGetSkill = 0` (it is trainer-taught and seeded in `InitSkills`), so it is the flag sweep, not the learn pass, that brings it back after a login. The pass is idempotent and learns nothing else a skill rewards (profession recipes and the client's helper spells stay out of the spellbook).
* **Trade Skills:** Bots receive two class-matched primary professions (Blacksmithing + Engineering for Warriors/Paladins; Skinning or Engineering + Leatherworking for Rogues/Hunters/Shamans/Druids; one of four gathering/crafting pairs such as Herbalism + Alchemy for casters) plus First Aid, Cooking, and Fishing.
* **Persistence:** Seeding runs once per fresh bot (it is skipped when the bot already has a primary profession or has played time); the professions and skills themselves are saved as normal character data, so they are never re-rolled across restarts.

### Fresh-Bot Field Kit (bags, tools, mounts, money, bandages)

Every fresh seed (pool login via `MakeComplete`, hire via `ProvisionSpellsAndGear`) also grants a usable field kit, idempotently — re-seeds only fill gaps, never duplicate:
* **Bags:** level-tier vendor bags in the plain container slots (6-slot to 16-slot by level band). Hunter quiver/ammo-pouch slots are untouched.
* **Profession tools:** mining pick, skinning knife, blacksmith hammer, arclight spanner and fishing pole, matching the bot's professions. Mining/skinning/fishing cannot run without them.
* **Mounts:** Swift Riding Turtle (spell 30174) at `AiPlayerbot.TurtleMountAtLevel` (default 18) through 39, race/class mount spell at 40 (apprentice) and 60 (journeyman); riding skill is already level-gated. Masterless random bots also earn the turtle on level-up. Mount speed follows riding skill like every player mount (riding 0 -> level/2, 75 -> 60%, 150 -> 100%), so level-60 bots ride at 100% and 40-59 bots at 60%.
* **Money:** a level-scaled starting amount on first seed only — never refilled on re-seed, so vendor/AH/repair economy stays earned.
* **Bandages:** one half-stack at the First Aid tier; class reagents, food/drink and potions come from the existing seed tables.
* **Hired-companion restock:** hired companions get a cheap hourly top-up (reagents, food/drink, potions, bandages, each bounded to a small stack). Tools and bags stay one-time seed.

---

## 3. Fresh-Bot Level Seed

A common issue with bot realms is a whole pool stuck at level 1 while you level a fresh character. TortoiseBots seeds each fresh pool bot once, on its first login, at a random level in `AiPlayerbot.RandomBotStartLevelMin`/`Max` (default 1–60, so the realm has bots at every level; for example 10–15 for a test pool, 1/1 keeps the historic level-1 start). With `AiPlayerbot.LevelLadder` (on by default) the online share is also spread by level band:

* Bots level up normally from their seed through grinding, questing, and XP.
* The login scatter (`AiPlayerbot.EnableRandomTeleports`, on by default) runs after the seed, so a seeded bot of level 10+ is placed in a zone fitting its level. Lower-level bots stay in their starting area, and pinned bots or bots already teleporting are skipped.

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

Switches: `AiPlayerbot.AhMarketEnabled = 1` turns on bots posting and bidding with their own inventories (random bots must be logged in, i.e. `AiPlayerbot.RandomBotAutologin = 1`). The server-generated extras are separate and off by default: `AiPlayerbot.AhMarketSyntheticSupply = 1` (synthetic listings) and `AiPlayerbot.AhMarketBuyer = 1` (synthetic buyer).

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
* Config: **`AiPlayerbot.RandomBotLftEnabled = 1`** (on by default; set `0` to opt out)
* When human players queue in the LFT tool and sit waiting for missing roles (especially Tanks or Healers), the module checks idle random bots in the world matching that level bracket. Random bots only fill while a real human waits — never autonomously.
* Eligible bots auto-queue and accept the dungeon invite; the group then walks to the dungeon together (bots follow their master into the portal via the master's area-trigger). There is **no teleport or summon**: bots far from the portal need `.bot summon`.
* Role match is fail-closed on each bot's own spec: a tank slot needs a natural tank with a shield actually equipped (best shield from bags is equipped first), a druid tank needs bear form, a healer needs healing spells. A missing role stays empty with a logged skip reason instead of being filled badly. Set `AiPlayerbot.RandomBotLftAllowRoleBorrow = 1` only to opt back into respec-into-role borrowing.

### Dungeons With Your Own Party Bots
* Queue in the LFT tool with your own managed bots in the party (alts/hires): the core auto-answers the rolecheck for them — your explicit `.bot role <Name> <tank|healer|dps|clear>` (or `role self`, same override) wins first, then the role/spec buttons (TBM party frame or `.bot command <Name> <strat>`): named combat specs answer (`protection`/`tank feral` tank; `holy`/`discipline`/`restoration` heal; every DPS spec dps — ambient `tank assist`/`dps assist` ignored). With neither set, automatic spec/gear detection applies.
* Your bots **auto-accept the dungeon offer** on the fill service's tick cadence, so the 90s native offer timer no longer expires on them. This works whether `RandomBotLftEnabled` is on or off; the random-fill switch only gates random-pool fill, not your party.
* After the group forms, bots follow you to the portal — bring them along or `.bot summon` stragglers first, since there is no automatic teleport.

### Battleground Auto-Queue
* Config: **`AiPlayerbot.RandomBotBgEnabled = 1`** (on by default; set `0` to opt out)
* Monitors PvP queues for **Warsong Gulch (WSG)**, **Arathi Basin (AB)**, and **Alterac Valley (AV)**.
* When real players queue up, random bots queue to balance faction team sizes and launch the battleground, allowing you to play active PvP battlegrounds even on low-population private servers. Random bots only queue while a real human waits in the queue — never autonomously.

### Battlegrounds With Your Own Party Bots
* Queue at the battlemaster with **Join as Group**: the core group-join check passes headless bot members like any player (same team, in world, level bracket, no deserter), so your party — you plus your own managed bots — enters the queue together. **WSG and AB** support group joins; **AV rejects group joins in the core**, so queue AV solo alongside your bots instead.
* Your bots **auto-accept the BG invite** (`STATUS_WAIT_JOIN` → port) through the same `bg status` packet action random bots use, then drop `follow` and run the per-map BG strategies (`warsong`/`arathi`/`alterac` + `pvp`) for the duration of the match.
* The auto-queue service never touches your party bots: master-reclaim reconciliation and lease eviction only cancel queue entries the service itself queued, and human-demand accounting counts only non-headless real players — your bots (or random fill bots) never create fake demand.

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

# Fresh-bot level seed (verified 10–15 test pool)
AiPlayerbot.RandomBotStartLevelMin = 10
AiPlayerbot.RandomBotStartLevelMax = 15

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
---

## 8. Resetting the Managed Bot Pool

Changes to fresh-character seeding, starter gear, professions, or skills only reach bots that are created afterwards. To rebuild the existing pool — same login accounts, brand-new characters — set a one-shot generation token and restart:

```ini
# conf/aiplayerbot.conf
AiPlayerbot.RandomBotPoolReset = once:gear-seeding-v2
AiPlayerbot.RandomBotAutoCreate = 1
```

The reset runs **only** during initial world startup, deletes the characters on managed pool accounts one at a time on the world thread, verifies that nothing is left, records the generation, and then regenerates the pool toward `MinRandomBots`/`MaxRandomBots`. `.reload config` never starts a reset, and there is no live reset command — a running world is never mutated behind the players' backs.

### Modes

| Value | Behaviour |
| :--- | :--- |
| `off` | Never reset automatically. This is the default and the recommended production setting. |
| `once:<token>` | Reset on the next server start **only if** `<token>` differs from the last completed generation. Re-using the same token is a no-op, so the value can safely stay in the config. Change the token for every new rebuild. |
| `always` | Reset on **every** server start. Development only — every restart destroys bot progression. |
| anything else | Invalid: it is logged and nothing is reset. |

### Step-by-step (existing installation)

```text
1.  Back up the character database.
2.  Start the server with AiPlayerbot.RandomBotPoolReset = off.
3.  At the server console run: bot pool status
4.  Upgrading an old installation? Run: bot pool adopt preview
5.  Review the listed accounts and run the exact confirmation command it prints:
        bot pool adopt confirm <challenge>
6.  Run: bot pool status        (managed accounts should now match)
7.  Stop the server normally.
8.  Set AiPlayerbot.RandomBotPoolReset = once:<new-token>
9.  Start the server and watch the log for:
        TortoiseBots: random pool generation '<new-token>'; reset scheduled for ...
        TortoiseBots: random pool reset progress: ...
        TortoiseBots: random pool reset verified: 0 characters remain ...
        TortoiseBots: random pool generation '<new-token>' applied; pool rebuild starts now
10. Run: bot pool status        (phase complete, characters regenerating)
```

Adoption (`bot pool adopt preview` / `confirm`) only **registers** accounts. It never deletes, moves, or edits a character, and it is available only at the server console.

### What a reset intentionally loses

- bot level, gear, bags, bank, quests, and profession progression;
- hired companions that were riding the pool (any hire character still on a pool account — its hire-ledger and durable ownership rows are cleared with the character);
- pinned bot names and their saved GUIDs (pins resolve again once new characters exist);
- guilds that consist only of pool characters (a guild holding anyone outside the pool blocks the reset instead — see below);
- bot-owned auction listings: active bidders are refunded through the core's normal auction mail before the listing is removed, and the listed item is destroyed with the character.

### Safety guarantees

- Only registered pool accounts are ever touched; `RNDBOTPersonal`-style personal accounts stay untouched unless an administrator adopts them.
- Every registered account is re-validated against the login database (existence **and** username) before the first deletion; a missing, renamed, or unreadable account aborts the reset.
- Human sessions are never logged out or deleted: a pool character being played by a player aborts the reset before anything is deleted.
- A bot-led guild with any member outside the pool aborts the reset and names the guild and its leader.
- The generation is recorded only after deletion **and** verification succeed, so a crash mid-reset resumes on the next start instead of being marked as done.
- While a reset is running (or after it fails), hiring, auto-create, autologin, and battleground selection stay paused. The pool is also unavailable whenever the registry itself cannot be validated, so nothing creates or hires pool characters while the module cannot tell its own accounts apart.
- Every auction owned by a pool character is settled in a dedicated phase **before the first deletion**, while all pool bidders still exist. A pool bot bidding on another pool bot's listing therefore cannot block the reset (its refund is issued while it is still there to receive it), and a bid that still cannot be refunded stops the reset instead of being lost silently. A hardcore bidder is deliberately not refunded — the same rule the core applies when an auction is cancelled. A listing that somehow appears after settlement stops the reset rather than being deleted unrefunded.
- An empty managed set (nothing registered, or no characters on the registered accounts) is a verified no-op: the generation is still recorded, so a token cannot silently wipe a pool that auto-create builds afterwards.
- Login accounts are always retained; only their characters are rebuilt.
