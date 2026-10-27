---
id: play-commands-roster-and-hiring
title: Roster & Hiring Commands
category: play
summary: Commands for managing owned alts, hiring mercenary companions, and recruiter services.
tags: [commands, roster, hiring, mercenaries]
relates_to:
  - play-commands
  - play-getting-started
  - server-settings-hiring
  - server-settings-party
---

# Roster & Hiring Commands

TortoiseBots provides tools to manage owned alternate characters (alts) as bots, hire temporary mercenary companions, and customize group compositions.

## Summary

| Command | Parameters / Syntax | What It Does |
| :--- | :--- | :--- |
| [`.bot roster`](#bot-roster) | None | Displays owned character states and active CC marks. |
| [`.bot add`](#bot-add) | `<Name>` | Logs in an owned alt as an active companion bot. |
| [`.bot remove`](#bot-remove) | `<Name>` | Logs out an owned bot or dismisses a hired companion. |
| [`.bot hire`](#bot-hire) | `<class> [role] [race] [gender] [tier] [level=<n>]` | Recruits a mercenary companion from a recruiter. |
| [`.bot hire raid`](#bot-hire-raid) | `<preset\|composition> [tier]` | Recruits up to 39 mercenary companions for a raid. |
| [`.bot role`](#bot-role) | `<Name\|self> <tank\|healer\|dps\|clear>` | Sets group role overrides for bots or the player. |
| [`.bot invite`](#bot-invite) | `<Name>` | Sends a party invite to an online owned bot. |
| [`.bot uninvite`](#bot-uninvite) | `<Name>` | Removes an owned bot or dismisses a companion. |
| [`.bot summon`](#bot-summon) | `<Name>` | Teleports an owned bot or hire to your coordinates. |
| [`.bot maintenance`](#bot-maintenance) | `<Name>` | Refreshes bot reagents, ammunition, and gear repairs. |
| [`.bot autogear`](#bot-autogear) | `<Name> [quality\|ilvl]` | Automatically upgrades gear to level-appropriate items. |
| [`.bot gear`](#bot-gear) | `<Name\|raid Commander> tier\|preset` | Gears a bot or a bound raid to a hire tier under an item-level cap (GM). |
| [`.bot release`](#bot-release) | None | Commands dead companion bots to release spirit. |
| [`.bot corpse run`](#bot-corpse-run) | None | Commands spirit bots to return to their corpses. |
| [`.bot learn`](#bot-learn) | None | Commands nearby companion bots to train new spells. |
| [`.bot trade`](#bot-trade) | Target Bot | Opens a trade window with a targeted companion. |

---

## Roster and Lifecycle Commands

### `.bot roster`

Emits an authoritative snapshot of all characters on your account across structured protocol streams: `TBM:ROSTER_BEGIN/END`, `TBM:ALTBOTS`, `TBM:CC_ASSIGN_BEGIN/END`, and `TBM:CLAIMED_BEGIN/END`.

```text
.bot roster
```

- **Character States:** Reports account character lifecycle states (`offline`, `online`, `starting`, `removing`) along with a distinct `grouped` flag indicating party membership.
- **CC Snapshot:** Lists active crowd control mark assignments for party bots. A bot is included only if it is in your party, has the `cc` combat strategy active, and has a mark set (since Moon is the default, mark presence alone is insufficient). Hires in your party are included in CC assignments even though they are omitted from the account character roster. Ungrouped alts are excluded.

### `.bot add`

Logs in an owned alternate character from your account as a headless companion bot.

```text
.bot add <CharacterName>
```

The character enters the world and follows you; invite it to your group with `.bot invite <CharacterName>`. To play the character yourself, simply log into it from character selection; the server cleanly disconnects the bot session and logs your human client in.

### `.bot remove`

Logs out an active companion bot.

```text
.bot remove <CharacterName>
```

Aliases include `.bot logout` and `.bot rm`. When used on an **owned alt**, the character safely logs out and retains all progress. When used on a **hired companion**, the hire agreement ends and the temporary character is deleted immediately.

### `.bot hire`

Recruits a fresh mercenary companion from a Mercenary Recruiter NPC.

```text
.bot hire <class> [role] [race] [gender] [tier] [level=<n>]
```

By default (`AiPlayerbot.HireAnywhere = 0`), the `.bot hire` chat command always refuses — you must use the recruiter's gossip dialogue at an inn or major city. Setting [`AiPlayerbot.HireAnywhere`](../../server/settings/hiring.md#aiplayerbothireanywhere) to `1` allows the chat command to work anywhere.

<details>
<summary>Hiring options, level syntax, and equipment tiers</summary>

- **Level Syntax & Clamping:** Companions match your level by default (`0` means hirer's level). Specify levels using `level=`, `level:`, `lvl=`, `lvl:`, bare `level <n>`, or a bare number $\ge 12$ as the last argument. Levels requested above the server cap are automatically lowered with notice; invalid forms (`level=0`, non-numeric, double level) are refused; a bare number anywhere but last is refused (`Unclear number … put the level last`).
- **Tier & Druid Spec Words:** Accepts tier words `t<N>`, `tier<N>`, or instance names `mc`, `bwl`, `zg`, `aq`, `naxx`, `kara`. Druid specs can be specified as `cat`, `balance`, `bear`, or `feral`. `.bot hire shaman tank` hires the experimental shaman tank (level 10 and above; in the recruiter menu it is "Enhancement (Tank) (experimental)"). It is refused while [`AiPlayerbot.ShamanTankEnabled`](../../server/settings/hiring.md#aiplayerbotshamantankenabled) is `0`.
- **Intro & Respec:** Once it is grouped with you (and in world), it posts its spec/spell intro once in party chat. Hiring a tier kit respecs talents and refits equipment.
- **Equipment Tiers:** Level 60 companions can be hired in fixed Best-in-Slot gear sets (Tiers 0 through 7). Tiers unlock per character via recruiter quests (or account-wide if [`AiPlayerbot.HireGearUnlockAccountWide`](../../server/settings/hiring.md#aiplayerbothiregearunlockaccountwide) is enabled): Tier 0 (Standard), Tier 1 (Pre-raid), Tier 2 (Zul'Gurub), Tier 3 (Molten Core), Tier 4 (Blackwing Lair), Tier 5 (Ahn'Qiraj), Tier 6 (Naxxramas), Tier 7 (Karazhan).
- **Temporary Character:** Hires exist while you are online, plus a disconnect grace period ([`AiPlayerbot.HireDisconnectGracePeriod`](../../server/settings/hiring.md#aiplayerbothiredisconnectgraceperiod)). Removing them deletes the temporary character.
- **PvE-only:** Hired companions leave you when you enter a battleground ([`AiPlayerbot.HireAllowPvP`](../../server/settings/hiring.md#aiplayerbothireallowpvp) = `1` keeps today's behaviour).
</details>

### `.bot hire raid`

Recruits up to 39 mercenary companions for a raid in a single operation.

```text
.bot hire raid <preset|composition> [tier]
```

Requires [`AiPlayerbot.HireAnywhere = 1`](../../server/settings/hiring.md#aiplayerbothireanywhere) even at a recruiter (proximity alone does not enable it). The command requires party leader permissions when the party must be converted to a raid (a party that would overflow 5); otherwise no leadership needed. It converts your group to a raid if needed, and checks available raid capacity and seats.

<details>
<summary>Raid compositions, batching, and quotes</summary>

- **Compositions:** Specify counts by role (e.g. `.bot hire raid 3t 12h 24d`) or combine roles and classes (e.g. `.bot hire raid 3 warrior:tank 12 priest:healer 19 hunter 5 mage`). Standard presets: `zg`, `aq20`, `onyxia`, `mc`, `bwl`, `aq40`, `naxx`.
- **Upfront Quote:** Total cost is quoted upfront. If you lack sufficient gold, the command reports `Nothing was hired` and aborts without creating partial bots.
- **Batching & Pause:** Bots are spawned 2 per 250 ms in priority order: tanks first, then healers, then DPS. Spawning automatically pauses while the player is on a loading screen and stops if the player leaves the group.
- **Summary:** Emits a final `Hired N...` message and `TBM:HIRE_RAID` protocol summary.

</details>

### `.bot role`

Overrides party role assignments used by the AI strategy engine to select specs and rotations.

```text
.bot role <Name|self> <tank|healer|dps|clear>
```

- `<Name>`: Sets the role for a specific companion bot and rebuilds its combat strategy kit. You may use your own character name instead of `self`. `.bot role self` sets the requesting player's own runtime role override and does not target a bot.
- `tank`, `healer` (alias `heal`), `dps`: Sets the designated combat role. `tank` on a shaman bot installs the shaman tank kit.
- Class-spec ids (`arms`, `fury`, `protection`, `holy`, `retribution`, `discipline`, `shadow`, `hybrid`, `restoration`, `balance`, `boomkin`, `cat`, `bear`, `bm`, `mm`, `survival`, `combat`, `assa`, `subtlety`, `ele`, `enh`, `frost`, `fire`, `arcane`, `affliction`, `demonology`, `destruction`): map to the role and apply that spec's strategies for the bot's own class, so `holy`, `protection` and `restoration` mean the right spec on each class. Rejected when the bot's class has no such spec.
- `clear` (aliases `none`, `reset`): Clears the override and restores automatic role inference based on talents and gear.

### `.bot invite`

Sends a party invitation to an online companion bot owned by your account.

```text
.bot invite <CharacterName>
```

### `.bot uninvite`

Removes a companion bot from your current party.

```text
.bot uninvite <CharacterName>
```

Aliases include `.bot kick`. If used on an owned alt while [`AiPlayerbot.AltBots`](../../server/settings/party.md#aiplayerbotaltbots) is enabled, the alt enters autonomous play. Removing a hired companion immediately terminates the hire and deletes the character.

### `.bot summon`

Teleports an owned bot or hired companion safely to your current location out of combat.

```text
.bot summon <CharacterName>
```

Teleportation takes 3 seconds. Upon arrival, the bot resumes following you unless you issued an explicit order (`stay`, `guard`, `hold`, or `comestay`) after the summon request, in which case the bot anchors where it landed (issuing `come` later re-enables follow-restore). <a id="automatic-summon-on-group-accept"></a>When [`AiPlayerbot.OwnedBotSummonWhenGroup`](../../server/settings/party.md#aiplayerbotownedbotsummonwhengroup) is enabled, owned bots and hires that accept a party invite are automatically summoned if on the same map beyond sight distance (governed by [`AiPlayerbot.OwnedBotSummonCooldown`](../../server/settings/party.md#aiplayerbotownedbotsummoncooldown), [`AiPlayerbot.OwnedBotSummonAllowInCombat`](../../server/settings/party.md#aiplayerbotownedbotsummonallowincombat), [`AiPlayerbot.OwnedBotSummonAllowMasterDead`](../../server/settings/party.md#aiplayerbotownedbotsummonallowmasterdead), [`AiPlayerbot.OwnedBotSummonAllowBotDead`](../../server/settings/party.md#aiplayerbotownedbotsummonallowbotdead), [`AiPlayerbot.OwnedBotSummonRevive`](../../server/settings/party.md#aiplayerbotownedbotsummonrevive), and [`AiPlayerbot.OwnedBotSummonRepair`](../../server/settings/party.md#aiplayerbotownedbotsummonrepair); pool bots are never auto-summoned).

### `.bot maintenance`

Refreshes an owned bot's consumables, reagents, arrows or bullets, potions, food, bandages, thrown items, and repairs damaged gear.

```text
.bot maintenance <CharacterName>
```

Requires [`AiPlayerbot.OwnedBotMaintenanceEnabled`](../../server/settings/party.md#aiplayerbotownedbotmaintenanceenabled) to be enabled (1). Only applies to owned alts and hired companions, never random pool bots.

### `.bot autogear`

Evaluates a bot's level and automatically equips level-appropriate equipment up to configured quality and item level caps.

```text
.bot autogear <CharacterName> [quality|ilvl]
```

Requires [`AiPlayerbot.OwnedBotAutogearEnabled`](../../server/settings/party.md#aiplayerbotownedbotautogearenabled) to be enabled (1). Defaults to uncommon (green) gear unless specified (e.g. `blue` or `200`). Ranged weapons are selected to match existing ammunition in the bot's bags. Note: autogear hands out no ammo; hunter ammunition must be stocked separately.

For real players, use [`.autogear`](#autogear) instead — `.bot autogear` only works on owned or hired bots.

### `.bot gear`

Gears one bot, or every bot of a commander's bound group, to a hire gear tier under an item-level cap. GameMaster only, in game or from the console.

```text
.bot gear <Name> tier <0-7> [ilvl=<N>] [noenchants]
.bot gear raid <Commander> tier <0-7> [ilvl=<N>] [noenchants]
.bot gear <Name|raid <Commander>> preset <name>
```

The named bot must be online and a bot; `raid <Commander>` gears every bot of the commander's bound group (the lead bot's group when bound, else the commander's own group). Each bot must be level 60, alive and in the world; the kit is the hire tier for the bot's class and role, skipped slots are reported as capped when the cap holds them back. Bagged weapons and armor above the cap are destroyed so the bot cannot re-equip them, and real players are never targets.

Named presets live in [`AiPlayerbot.BotGearPresets`](../../server/settings/hiring.md#aiplayerbotbotgearpresets) (`lkh`, `zg`, `aq20`, `timbermaw`, `onyxia`, `mc`, `bwl`, `es`, `aq40`, `naxx`, `kara40`, `prebis`, `t1`, `t2`).

### `.autogear`

Gears a **real player** the way a spawned bot gets geared: items picked for the character's level and the chosen spec from the same gear pool the bots use, under the same item level and quality limits. Upgrades only — nothing already worn is lost.

```text
.autogear [player name] <spec> [ilvl]
```

Without a player name the command gears the selected player. The class comes from the character, so you only type the spec. The target must be a real player who is online, alive and out of combat; bots are refused. A druid must leave their shapeshift form first. Characters on a Turtle challenge mode (Hardcore, Slow & Steady, ...) are GM-only whatever the settings below are.

**Specs and aliases.** A tank variant can be typed with a hyphen or a space. Short aliases work too. An unknown or missing spec changes nothing and lists the valid specs for the target's class:

| Class | Specs | Short aliases |
| :--- | :--- | :--- |
| Warrior | `arms`, `fury`, `protection` | `prot`, `tank` |
| Paladin | `holy`, `protection`, `retribution` | `prot`, `ret` |
| Hunter | `beast`, `marksmanship`, `survival` | `bm`, `mm`, `surv` |
| Rogue | `assassination`, `combat`, `subtlety` | `sin`, `sub` |
| Priest | `discipline`, `holy`, `shadow` | `disc` |
| Shaman | `elemental`, `enhancement`, `enhancement-tank`, `restoration` | `ele`, `enh`, `resto` |
| Mage | `arcane`, `fire`, `frost` | — |
| Warlock | `affliction`, `demonology`, `destruction` | `afflic`, `demo`, `destro` |
| Druid | `balance`, `feral`, `feral-tank`, `restoration` | `moonkin`, `cat`, `bear`, `resto` |

**Upgrades only.** An item goes on only if it scores better for the chosen spec than what is in that slot. Replaced items go to the target's bags; when the bags are full they are mailed to the target with no expiry. Nothing is destroyed. A slot holding something better than the pool would give (for example a manually added epic) is left alone, and pieces of an item set that currently gives an active set bonus are kept whole. A two-handed weapon must beat the current main-hand **and** off-hand pieces together before it displaces the pair. A unique-equipped item is never worn twice (counting what is already equipped).

**Level and quality limits.** Only items the player can wear at their current level (required level ≤ player level). The default ceiling is the same item level and quality a spawned bot of that level gets (rare at most). The optional `ilvl` argument can only lower that ceiling — a higher value is clamped to the default and the reply says so. At level 60 (max level) the `ilvl` argument may also **raise** the ceiling, e.g. `.autogear Thrall protection 90` picks the best items up to item level 90 — epic included, since the point of raising is the best gear in the pool.

**Gear only.** No weapon skills, bags, ammo or consumables are given. If the equipped ranged weapon has no matching ammo in the bags, the command answers with the same missing-ammo warning `.bot autogear` gives.

**Who can use it.** By default GMs only, on any real online player including themselves. With [`AiPlayerbot.PlayerAutogearForAll`](../../server/settings/party.md#aiplayerbotplayerautogearforall) = `1`, every player can use it — but only on themselves. It is free and has no cooldown.

Examples:

```text
.autogear Thrall enhancement-tank        # GM gears Thrall as a shaman tank
.autogear enhancement tank               # gears the selected player (space form)
.autogear resto                          # gears yourself as resto (selected target)
.autogear <shaman> enhancement 15        # ceiling lowered to item level 15
.autogear <60 warrior> protection 90     # at level 60: ceiling raised to 90
```

### `.bot release`

Commands all dead companion bots in the party to release spirit to the graveyard. Also available as `.bot action release`.

```text
.bot release
```

### `.bot corpse run`

Commands companion bots in spirit form to path back to their corpse location or dungeon portal. Also available as `.bot action corpse run` and `.bot corpserun`.

```text
.bot corpse run
```

### `.bot learn`

Commands companion bots standing near class trainers to purchase all available, affordable spells and ability ranks. Also available as `.bot action learn`.

```text
.bot learn
```

Target the trainer NPC you have selected before issuing the command.

### `.bot trade`

Opens a direct trading window with your targeted companion bot. Also available as `.bot action trade`.

```text
.bot trade
```

---

## Alt-Bots: Autonomous Alts

When [`AiPlayerbot.AltBots`](../../server/settings/party.md#aiplayerbotaltbots) is enabled (1), your owned alternate characters can remain logged in and play autonomously in the open world.

- **Starting an Alt-Bot:** Log in an alt using `.bot add <Name>`, then remove it from your party with `.bot uninvite <Name>` (or disband the party while the alt is present). The bot announces it is playing autonomously and begins questing, grinding, traveling flight paths, taking boats/zeppelins, resting, gathering, and fishing like a pool bot.
- **Stopping an Alt-Bot:** Type `.bot remove <Name>` to log it out cleanly, take it back with `.bot add <Name>` without needing to relog, or log into the character yourself from the character selection screen. Upon stopping, the alt whispers you a summary of levels gained, quests completed, deaths, and gold earned.
- **Pool Slot Displacement:** Each active alt-bot displaces one random bot from the world pool, which is restored when the alt-bot stops. Alt-bots are tagged with the `alt` label on the dashboard and record `AltBotStart` and `AltBotEnd` events in `bot_events.csv`. Server crashes before clean logout may lose unsaved alt progress.
- **Relocation Rules:** Upon startup, if an alt-bot was left in a dungeon, battleground, or boat, it is relocated to the instance exit or nearest graveyard to avoid broken pathing.
- **Autonomous Restrictions:** Autonomous alts sell only gray junk and unneeded white items (never soulbound, equipped, or better-than-white gear); never abandon quests they did not take themselves; keep 10 quest-log slots free; never buy mounts, bags, or talent respecs; never use auction houses, mail, direct trade, or guild banks; and never destroy items. On loot rolls, they roll Greed or Pass.
- **Stranger & Guild Rules:** Strangers can invite your alt-bot and issue tactical orders, but cannot trade with it or log it out. An alt-bot accepts guild invitations only from its owner's account (or GM), and never leaves its guild autonomously. Grouping with an alt-bot binds it to existing instance locks.
- **Challenge Modes:** Hardcore characters are refused from becoming autonomous alt-bots to prevent character loss.

---

## Capital Recruiter Services

The six capital city Mercenary Recruiters (Stormwind, Ironforge, Darnassus, Orgrimmar, Undercity, Thunder Bluff) offer special services for level-60 characters.

### World Buffs at Capital Recruiters

Level-60 players can purchase world buffs for themselves and nearby group members within 40 yards. Each buff unlocks permanently for your character after completing a one-time quest:

| World Buff | Unlock Requirement | Purchase Cost |
| :--- | :--- | :--- |
| **Rallying Cry of the Dragonslayer** (2 h) | Slay Onyxia or Nefarian | 10g + 2g per person |
| **Warchief's Blessing** (1 h) | Slay Rend Blackhand | 10g + 2g per person |
| **Spirit of Zandalar** (2 h) | Slay Hakkar | 10g + 2g per person |
| **Dire Maul Tribute** (2 h, all three) | Earn guard's favor in Dire Maul | 10g + 2g per person |
| **Sayge's Dark Fortune** (2 h, choice of 8) | Consult Sayge at Darkmoon Faire | 6g + 1g per person |
| **Songflower Serenade** (1 h) | Cleanse songflower in Felwood | 4g + 1g per person |
| **Traces of Silithyst** (30 min) | Defeat 5 opposing players in Silithus | 2g + 40s per person |

- **Recruiter Rules:** Players must complete the unlock quest before purchasing; buffs provide no quest XP or rewards; world buff purchases only apply to party members (pool bots cannot purchase buffs, and bystanders outside the purchaser's party receive nothing); the menu shows the quest or the purchase for each buff, never both. See [Hiring and Recruiter Settings](../../server/settings/hiring.md).

### Mercenary Contracts

When [`AiPlayerbot.HireGearTiersEnabled`](../../server/settings/hiring.md#aiplayerbothiregeartiersenabled) is enabled (1), capital recruiters offer the **Mercenary Contracts** dialogue. This tracks seven progressive unlock quests (quest IDs `90100` through `90106` for Tiers 1 through 7) that grant access to high-tier mercenary equipment sets:

- Contracts must be open in your quest log to earn credit.
- Bots never earn unlock credit for themselves; humans in reward distance are still credited even if a bot lands the killing blow.
- Contracts unlock once per character ever. The Four Horsemen in Naxxramas counts as a single encounter kill when all four horsemen are defeated.
- Abandoning and retaking a contract restores previously recorded boss kill progress.
- Detailed tier equipment lists are documented in the [Hire Gear Reference](../../reference/hire-gear.md).
