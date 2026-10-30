---
id: server-settings-hiring
title: Mercenary Hiring & Recruiter Settings
category: server
summary: Configuration settings for hiring bot companions, gear tiers, contracts, and recruiter world buffs.
tags: [config, hiring, mercenaries, gear-tiers, world-buffs]
relates_to:
  - play-commands-roster-and-hiring
  - server-settings-party
---

# Mercenary Hiring & Recruiter Settings

These settings configure mercenary companion hiring, party cost scaling, raid presets, gear progression tiers, and recruiter world buffs. Settings go in `aiplayerbot.conf`; see [How configuration works](../configuration.md).

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.HireEnabled`](#aiplayerbothireenabled) | `1` | Master toggle for companion hiring commands and inn recruiters. |
| [`AiPlayerbot.HireMinAccountSecurity`](#aiplayerbothireminaccountsecurity) | `0` | Minimum account security level required to hire mercenary companions. |
| [`AiPlayerbot.HireFreeMinSecurity`](#aiplayerbothirefreeminsecurity) | `0` | Minimum account security level that receives free mercenary hiring. |
| [`AiPlayerbot.HireMaxBotsPerPlayer`](#aiplayerbothiremaxbotsperplayer) | `4` | Maximum number of hired companions a single player may lead. |
| [`AiPlayerbot.HireMaxLevelAboveHirer`](#aiplayerbothiremaxlevelabovehirer) | `0` | Maximum character levels a hired companion may exceed its employer. |
| [`AiPlayerbot.HireAnywhere`](#aiplayerbothireanywhere) | `0` | Allows hiring companions from anywhere via chat command. |
| [`AiPlayerbot.HireAllowPvP`](#aiplayerbothireallowpvp) | `0` | Allows hired companions into battlegrounds (off = PvE-only). |
| [`AiPlayerbot.HireLoot`](#aiplayerbothireloot) | `1` | Hired companions loot and roll on group loot (off = never loot, always pass). |
| [`AiPlayerbot.ShamanTankEnabled`](#aiplayerbotshamantankenabled) | `1` | Offers the experimental 7.3 shaman tank build at recruiters, by `.bot hire`, and to random pool shamans. |
| [`AiPlayerbot.HireBaseCostCopper`](#aiplayerbothirebasecostcopper) | `15000` | Base copper fee used for level-scaled party companion hiring. |
| [`AiPlayerbot.HirePartyMult2`](#aiplayerbothirepartymult2) | `1.66` | Cost multiplier applied when hiring a second party companion. |
| [`AiPlayerbot.HirePartyMult3`](#aiplayerbothirepartymult3) | `2.66` | Cost multiplier applied when hiring a third party companion. |
| [`AiPlayerbot.HirePartyMult4`](#aiplayerbothirepartymult4) | `4.66` | Cost multiplier applied when hiring a fourth party companion. |
| [`AiPlayerbot.HireRaidFlatCostCopper`](#aiplayerbothireraidflatcostcopper) | `10000` | Flat per-bot fee in copper for hiring raid companions beyond four. |
| [`AiPlayerbot.HireDisconnectGracePeriod`](#aiplayerbothiredisconnectgraceperiod) | `300` | Seconds a hired companion keeps guarding after disconnect before the hire ends. |
| [`AiPlayerbot.HireRaidSpread`](#aiplayerbothireraidspread) | `tank=warrior...;healer=priest...` | Class priority list per raid role for composition auto-fill. |
| [`AiPlayerbot.HireRaidPresets`](#aiplayerbothireraidpresets) | `zg=2t...;aq20=2t...;onyxia=3t...` | Named composition presets for one-command raid hiring. |
| [`AiPlayerbot.BotGearPresets`](#aiplayerbotbotgearpresets) | `prebis=t1...;lkh=t2...` | Named `.bot gear` tier and item-level cap presets. |
| [`AiPlayerbot.HireGearTiersEnabled`](#aiplayerbothiregeartiersenabled) | `1` | Enables pre-enchanted static gear tiers for hired companions. |
| [`AiPlayerbot.HireGearStartTier`](#aiplayerbothiregearstarttier) | `0` | Baseline gear tier available to all players without completing quests. |
| [`AiPlayerbot.HireGearUnlockAccountWide`](#aiplayerbothiregearunlockaccountwide) | `0` | Shares gear tier unlock quest completion across all account characters. |
| [`AiPlayerbot.HireGearAllQuestsAvailable`](#aiplayerbothiregearallquestsavailable) | `0` | Offers all seven gear unlock quests simultaneously rather than sequentially. |
| [`AiPlayerbot.HireGearOptionalMaps`](#aiplayerbothiregearoptionalmaps) | `""` | Comma-separated map IDs automatically credited for gear tier unlock quests. |
| [`AiPlayerbot.HireGearTierCostMultipliers`](#aiplayerbothiregeartiercostmultipliers) | `1,1.5,2,3,4,5,6,6` | Eight cost multipliers applied to hiring fees per gear tier. |
| [`AiPlayerbot.HireGearMaxItemLevelAboveLevel`](#aiplayerbothiregearmaxitemlevelabovelevel) | `5` | Item-level allowance above a standard (tier 0) hire's own level. |
| [`AiPlayerbot.WorldBuffsEnabled`](#aiplayerbotworldbuffsenabled) | `1` | Master toggle for purchasing world buffs at capital inn recruiters. |
| [`AiPlayerbot.WorldBuffsKeepInRaids`](#aiplayerbotworldbuffskeepinraids) | `0` | Preserves active world buffs when zoning into raid instances. |
| [`AiPlayerbot.WorldBuffsMinLevel`](#aiplayerbotworldbuffsminlevel) | `60` | Minimum character level required to access recruiter world buffs. |
| [`AiPlayerbot.WorldBuffsPriceBaseCopper`](#aiplayerbotworldbuffspricebasecopper) | `100000` | Base copper fee for standard capital recruiter world buffs. |
| [`AiPlayerbot.WorldBuffsPricePerPersonCopper`](#aiplayerbotworldbuffspriceperpersoncopper) | `20000` | Per-person copper fee added for standard recruiter world buffs. |
| [`AiPlayerbot.WorldBuffsSaygePriceBaseCopper`](#aiplayerbotworldbuffssaygepricebasecopper) | `60000` | Base copper fee for purchasing Sayge Dark Fortune world buffs. |
| [`AiPlayerbot.WorldBuffsSaygePricePerPersonCopper`](#aiplayerbotworldbuffssaygepriceperpersoncopper) | `10000` | Per-person copper fee added for Sayge Dark Fortune world buffs. |
| [`AiPlayerbot.WorldBuffsSongflowerPriceBaseCopper`](#aiplayerbotworldbuffssongflowerpricebasecopper) | `40000` | Base copper fee for purchasing Songflower Serenade world buffs. |
| [`AiPlayerbot.WorldBuffsSongflowerPricePerPersonCopper`](#aiplayerbotworldbuffssongflowerpriceperpersoncopper) | `10000` | Per-person copper fee added for Songflower Serenade world buffs. |
| [`AiPlayerbot.WorldBuffsSilithystPriceBaseCopper`](#aiplayerbotworldbuffssilithystpricebasecopper) | `20000` | Base copper fee for purchasing Traces of Silithyst world buffs. |
| [`AiPlayerbot.WorldBuffsSilithystPricePerPersonCopper`](#aiplayerbotworldbuffssilithystpriceperpersoncopper) | `4000` | Per-person copper fee added for Traces of Silithyst world buffs. |
| [`AiPlayerbot.WorldBuffsUnlockFeeRaidCopper`](#aiplayerbotworldbuffsunlockfeeraidcopper) | `2000000` | Turn-in copper fee for raid boss world buff unlock quests. |
| [`AiPlayerbot.WorldBuffsUnlockFeeCopper`](#aiplayerbotworldbuffsunlockfeecopper) | `1000000` | Turn-in copper fee for non-raid world buff unlock quests. |

## Settings

### `AiPlayerbot.HireEnabled`
Master switch for mercenary companion hiring via `.bot hire` and capital inn recruiters. Disable this if you want only owned alts or random pool bots.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Enables on-demand companion hiring through the `.bot hire` chat command and through `<Mercenary Hire>` recruiter NPCs stationed at major inns and capital cities.

When set to `0`, all hiring interactions and recruiter menus are disabled.

</details>

### `AiPlayerbot.HireMinAccountSecurity`
Minimum account security level required to hire mercenary companions. Raise this to restrict hiring to server staff or privileged accounts.
**Default:** `0` · **Values:** `0` player, `1` observer, `2` moderator, `3` developer, `4` administrator (values `0` to `6`; no separate GM rank) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Restricts the ability to hire companions based on the user account security level.

The default value of `0` permits all standard players to hire companions. Setting this to `1` or higher restricts hiring to staff members.

</details>

### `AiPlayerbot.HireFreeMinSecurity`
Enables free companion hiring for staff accounts meeting or exceeding this security level.
**Default:** `0` · **Values:** `0` off (everyone pays), `1` observer, `2` moderator, `3` developer, `4` administrator (values `0` to `6`; no separate GM rank) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Accounts with a security level equal to or greater than this setting hire companions without paying copper.

No gold is deducted from the player, and recruiter dialogue menus and command outputs display `free (GM)`. The default value of `0` turns free hiring off so that every player pays the full calculated cost.

</details>

### `AiPlayerbot.HireMaxBotsPerPlayer`
Caps the maximum number of hired companions a single player can maintain simultaneously.
**Default:** `4` · **Values:** `0` to `39` (`0` disables companion hiring) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Limits how many active mercenary companions an individual player can command at one time.

The default of `4` supports a full five-player dungeon group. For raid environments, increase this value up to `39` to allow assembling full 40-player raid rosters.

The commander of a bound raid (`.bot commander bind <LeadBot> raid`) stands outside the raid group, so his raid holds 40 bots. At `39` he may hire a 40th companion into the bound raid; a lower value caps him too, and players in their own raid keep 39.

</details>

### `AiPlayerbot.HireMaxLevelAboveHirer`
Specifies how many levels above the hirer a mercenary companion may be when hired.
**Default:** `0` · **Values:** `0` to `59` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Limits the level of companions requested via `.bot hire ... level=<N>`.

When set to `0`, companions cannot exceed the hirer character level, though any level below the hirer down to level 1 is permitted. Game masters are exempt from this limit. When no explicit level is requested, companions match the hirer level.

</details>

### `AiPlayerbot.HireAnywhere`
Controls whether players can hire companions from any location or only at recruiter NPCs.
**Default:** `0` · **Values:** `0` recruiters only, `1` anywhere · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `0`, companions may only be hired while interacting in person with a `<Mercenary Hire>` recruiter NPC.

When set to `1`, players may execute `.bot hire` from anywhere in the world. Calculated hire fees and limits remain identical regardless of location.

</details>

### `AiPlayerbot.HireAllowPvP`
Controls whether hired companions may enter battlegrounds.
**Default:** `0` · **Values:** `0` PvE-only, `1` allowed in · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `0`, hired companions are PvE-only: queueing for a battleground warns the master once, entering dismisses the companions, hiring is refused while queued or inside, and hired companions never join the battleground queue on their own or through a group queue. Each recruiter's greeting ends with a red line saying its mercenaries are for PvE only; with `1` that line is left out.

When set to `1`, hires may go to battlegrounds as before.

</details>

### `AiPlayerbot.HireLoot`
Controls whether hired companions loot corpses and roll on group loot.
**Default:** `1` · **Values:** `1` loots, `0` never loots · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `1`, hired companions loot as before. When set to `0`, a hire never opens a corpse and passes on every roll, so the master and the party get all the loot. Hired companions never quest, whatever this is set to. Pool bots and owned alts are unaffected.

</details>

### `AiPlayerbot.ShamanTankEnabled`
Controls whether the experimental 7.3 shaman tank build (Enhancement (Tank)) is offered for hire and in the random pool.
**Default:** `1` · **Values:** `1` offered, `0` withheld · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `0`, the recruiter spec menu omits the tank option, `.bot hire shaman tank` is refused, and random pool shamans never roll the `7.3` premade (its `AiPlayerbot.PremadeSpecProb.7.3` weight counts as 0). Shamans already on the build keep it.

</details>

### `AiPlayerbot.HireBaseCostCopper`
Base cost in copper for hiring a level 60 companion. Lower levels scale downward proportionally.
**Default:** `15000` (1 gold 50 silver) · **Values:** non-negative integer (`0` for free hiring) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Establishes the foundation of the level-scaled hiring cost curve.

At level 60 with default settings, hiring a complete four-bot party costs approximately 15 gold total. Setting this value to `0` makes party hiring completely free.

</details>

### `AiPlayerbot.HirePartyMult2`
Cost multiplier applied to the base hire price when employing a second companion.
**Default:** `1.66` · **Values:** float (`0.0` to `100.0`) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Escalates the price of hiring a second party companion to reflect increasing party power.

The base cost is multiplied by this factor when quoting and charging the fee for the second companion.

</details>

### `AiPlayerbot.HirePartyMult3`
Cost multiplier applied to the base hire price when employing a third companion.
**Default:** `2.66` · **Values:** float (`0.0` to `100.0`) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Escalates the fee for the third party companion.

Combined with the earlier multipliers, this shapes the cost curve for small-group dungeon composition.

</details>

### `AiPlayerbot.HirePartyMult4`
Cost multiplier applied to the base hire price when employing a fourth companion.
**Default:** `4.66` · **Values:** float (`0.0` to `100.0`) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Escalates the fee for the fourth companion, completing a full five-player party roster.

Together, the four multipliers produce a progressive cost curve for full party assembly.

</details>

### `AiPlayerbot.HireRaidFlatCostCopper`
Flat per-bot fee charged for hiring raid companions beyond the initial four party members.
**Default:** `10000` (1 gold) · **Values:** non-negative integer · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When forming raid groups with five or more companions, each additional bot hired past the fourth is charged this flat rate at level 60 rather than using party escalation multipliers.

Companions below level 60 scale downward proportionally.

</details>

### `AiPlayerbot.HireDisconnectGracePeriod`
Grace period in seconds before a hire whose master went offline ends.
**Default:** `300` (5 minutes) · **Values:** seconds (`0..3600`) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When a player disconnects from the server, their hired companions hold position and guard their location for this duration.

If the player logs back in within the grace period, companions automatically rejoin their party. If the timer expires before they return, the hire ends and the companion character is deleted.

</details>

### `AiPlayerbot.HireRaidSpread`
Specifies class distribution lists used when automatically filling raid roles with `.bot hire raid`.
**Default:** `tank=warrior,druid,paladin;healer=priest,priest,paladin,shaman,druid;dps=mage,warlock,rogue,hunter,warrior,mage,warlock,rogue,hunter,shaman` · **Values:** semicolon-separated role mappings · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Defines the class pool distributed round-robin when a command specifies role counts like `3t 12h 24d`.

Classes unfieldable by your faction are automatically skipped. Repeating a class in the list increases its weighting during assignment. All three roles (`tank`, `healer`, `dps`) must be defined; malformed configurations log an error and revert to the default spread.

</details>

### `AiPlayerbot.HireRaidPresets`
Defines shorthand raid composition presets available for one-step raid hiring.
**Default:** `zg=2t 4h 13d t2;aq20=2t 4h 13d t5;onyxia=3t 10h 26d t3;mc=3t 10h 26d t3;bwl=3t 10h 26d t4;aq40=3t 10h 26d t5;naxx=3t 10h 26d t6` · **Values:** semicolon-separated preset definitions · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Allows players to hire complete pre-configured raid teams using `.bot hire raid <preset> [tier] [level=<n>]`.

Each preset specifies role counts and an optional default gear tier. Explicit gear tiers supplied in the command override the preset default. An optional `level=` hires the whole roster at that level (clamped like `.bot hire`); without it the companions arrive at your level. Malformed entries log an error and use the built-in defaults.

</details>

### `AiPlayerbot.BotGearPresets`
Named gear presets for `.bot gear <target> preset <name>`: `name=t<tier> [ilvl=<cap>]`, `;`-separated.
**Default:** `prebis=t1 ilvl=63;lkh=t2 ilvl=63;zg=t2 ilvl=63;aq20=t2 ilvl=63;t1=t3 ilvl=66;timbermaw=t3 ilvl=66;onyxia=t3 ilvl=66;mc=t3 ilvl=66;t2=t4 ilvl=76;bwl=t4 ilvl=76;es=t4 ilvl=76;aq40=t5 ilvl=78;naxx=t6 ilvl=83;kara40=t7 ilvl=88` · **Values:** semicolon-separated preset definitions · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Each preset fixes the hire gear tier and the item-level cap `.bot gear` applies: `zg` gears tier 2 with every item at or under item level 63. A malformed value logs an error and uses the built-in default.

</details>

### `AiPlayerbot.HireGearTiersEnabled`
Master switch for static best-in-slot gear progression tiers on hired companions.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Enables pre-enchanted static equipment tiers (tiers 1 through 7) for level 60 hired companions.

When set to `0`, companions receive only randomly generated level-appropriate gear without tier progression or enchants.

</details>

### `AiPlayerbot.HireGearStartTier`
Specifies the baseline gear tier accessible to all players without completing unlock quests.
**Default:** `0` · **Values:** `0` to `7` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Determines the starting gear tier available on fresh accounts. The tier actually offered is the higher of this setting and the player's highest rewarded unlock quest.

At tier `0`, companions arrive in standard randomly-generated kit (tier 0 'Standard kit'). At tier 1 or higher, companions arrive in static pre-enchanted gear sets. Setting this to `7` unlocks the entire gear ladder immediately without requiring quest turn-ins.

</details>

### `AiPlayerbot.HireGearUnlockAccountWide`
Controls whether gear tier quest unlocks are shared across all characters on an account.
**Default:** `0` · **Values:** `0` per-character, `1` account-wide · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `1`, completing a gear tier unlock quest on any character unlocks that tier for every character on the same account.

When set to `0`, each character must complete the recruiter unlock quests individually.

</details>

### `AiPlayerbot.HireGearAllQuestsAvailable`
Controls whether recruiter gear unlock quests must be completed in order or are all offered at once.
**Default:** `0` · **Values:** `0` sequential, `1` all available · **Restart:** needed (live after the next restart)

<details>
<summary>How it works in detail</summary>

When set to `0`, the seven gear unlock quests form a linear chain where unlocking tier N requires completing tier N-1 first.

When set to `1`, recruiters offer all seven tier unlock quests simultaneously. Changes write directly into quest templates at server start and take effect upon the subsequent restart.

</details>

### `AiPlayerbot.HireGearOptionalMaps`
Comma-separated list of map IDs whose bosses are credited automatically when an unlock quest is accepted.
**Default:** `""` (empty) · **Values:** comma-separated map IDs (e.g. `819`) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Allows server administrators to bypass specific raid or dungeon maps for gear tier unlock requirements if those encounters are unscripted or overtuned.

Bosses located on the listed maps are automatically marked complete upon accepting the quest. Items from those maps remain in the candidate equipment database.

</details>

### `AiPlayerbot.HireGearTierCostMultipliers`
Specifies eight comma-separated cost multipliers applied to base hiring fees for gear tiers 0 through 7.
**Default:** `1,1.5,2,3,4,5,6,6` · **Values:** eight comma-separated non-negative numbers · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Scales the quoted and charged hire fee based on the chosen gear tier of the companion.

The fee charged equals the base hire fee multiplied by the multiplier for that tier. The string must contain exactly eight non-negative numbers; invalid strings log an error and use default multipliers.

</details>

### `AiPlayerbot.HireGearMaxItemLevelAboveLevel`
Caps generated standard-kit hire gear relative to the companion's own level.
**Default:** `5` · **Values:** integer item levels · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The randomly generated kit a tier 0 companion arrives in must not exceed `hire level + this value`, and never exceeds `AiPlayerbot.RandomGearMaxLevel`. The default `5` sits a little above the pool-bot rule (`AiPlayerbot.RandomGearMaxItemLevelAboveLevel`, `1`): a level-60 standard hire tops out at item level 65, the tier-1 curated ceiling and below the tier-2 kit at 71.

Only the standard (tier 0) kit is affected. The curated tiers 1-7 (`tortoise_bots_hire_gear`) and gear the companion earns itself are not capped. `0` keeps `AiPlayerbot.RandomGearMaxLevel` as the only ceiling.

</details>

### `AiPlayerbot.WorldBuffsEnabled`
Master switch for purchasing world buffs from recruiter NPCs in capital cities.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Enables the `World buffs` dialogue branch at `<Mercenary Hire>` recruiters stationed in capital cities for players meeting `AiPlayerbot.WorldBuffsMinLevel` (60 by default).

When set to `0`, recruiter dialogue menus omit the world buffs branch entirely.

</details>

### `AiPlayerbot.WorldBuffsKeepInRaids`
Controls whether world buffs are preserved or stripped when entering raid instances.
**Default:** `0` · **Values:** `0` strip buffs, `1` preserve buffs · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set to `1`, entering a raid portal preserves active world buffs on players and bots, restoring their remaining durations.

When set to `0`, the core engine strips world buffs upon entering raids, and the module additionally strips them upon entering Upper Karazhan.

</details>

### `AiPlayerbot.WorldBuffsMinLevel`
Minimum character level required to view and purchase world buffs at capital recruiters.
**Default:** `60` · **Values:** `0` to `60` (clamped `>60`) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Restricts the visibility of the recruiter world buffs dialogue branch to characters at or above this level.

The default of `60` reserves recruiter buffs for end-game progression.

</details>

### `AiPlayerbot.WorldBuffsPriceBaseCopper`
Base copper fee for purchasing standard raid boss world buffs from recruiters.
**Default:** `100000` (10 gold) · **Values:** non-negative integer · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Sets the base price for purchasing Rallying Cry of the Dragonslayer, Warchief's Blessing, Spirit of Zandalar, or the complete Dire Maul Tribute package.

The total price charged equals this base fee plus `AiPlayerbot.WorldBuffsPricePerPersonCopper` multiplied by the number of nearby group/raid members.

</details>

### `AiPlayerbot.WorldBuffsPricePerPersonCopper`
Per-person copper fee added to standard recruiter world buff purchases.
**Default:** `20000` (2 gold) · **Values:** non-negative integer · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Added to the base purchase price for each group or raid member standing within 40 yards of the recruiter during purchase.

A solo player pays the base fee plus one personal share (12 gold total at defaults). Only living group or raid members in the world and in range are counted; random bystanders get nothing.

</details>

### `AiPlayerbot.WorldBuffsSaygePriceBaseCopper`
Base copper fee for purchasing Sayge Dark Fortune buffs at capital recruiters.
**Default:** `60000` (6 gold) · **Values:** non-negative integer · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Base cost charged when selecting one of the eight Darkmoon Faire Sayge fortunes from the recruiter submenu.

The chosen fortune replaces any active Sayge fortune currently on the character.

</details>

### `AiPlayerbot.WorldBuffsSaygePricePerPersonCopper`
Per-person copper fee added when purchasing Sayge Dark Fortune buffs.
**Default:** `10000` (1 gold) · **Values:** non-negative integer · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Added to the Sayge base fee for each eligible group or raid member standing within 40 yards of the recruiter.

</details>

### `AiPlayerbot.WorldBuffsSongflowerPriceBaseCopper`
Base copper fee for purchasing the Songflower Serenade world buff at capital recruiters.
**Default:** `40000` (4 gold) · **Values:** non-negative integer · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Base price charged when purchasing the Felwood Songflower Serenade buff from recruiter NPCs.

</details>

### `AiPlayerbot.WorldBuffsSongflowerPricePerPersonCopper`
Per-person copper fee added when purchasing Songflower Serenade.
**Default:** `10000` (1 gold) · **Values:** non-negative integer · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Added to the Songflower base fee for each group or raid member standing within 40 yards of the recruiter.

</details>

### `AiPlayerbot.WorldBuffsSilithystPriceBaseCopper`
Base copper fee for purchasing the Traces of Silithyst world buff at capital recruiters.
**Default:** `20000` (2 gold) · **Values:** non-negative integer · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Base price charged when purchasing the Traces of Silithyst PvP buff from recruiter NPCs.

</details>

### `AiPlayerbot.WorldBuffsSilithystPricePerPersonCopper`
Per-person copper fee added when purchasing Traces of Silithyst.
**Default:** `4000` (40 silver) · **Values:** non-negative integer · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Added to the Silithyst base fee for each group or raid member standing within 40 yards of the recruiter.

</details>

### `AiPlayerbot.WorldBuffsUnlockFeeRaidCopper`
Turn-in copper fee required to complete unlock quests for raid boss world buffs.
**Default:** `2000000` (200 gold) · **Values:** non-negative integer (`0` for free) · **Restart:** needed (requires two restarts)

<details>
<summary>How it works in detail</summary>

Turn-in fee required to complete unlock quests for Rallying Cry of the Dragonslayer, Warchief's Blessing, and Spirit of Zandalar.

Fees are written into the quest templates at server startup. Modifying this setting requires two full server restarts before the updated gold cost appears in the quest dialogue.

</details>

### `AiPlayerbot.WorldBuffsUnlockFeeCopper`
Turn-in copper fee required to complete unlock quests for non-raid world buffs.
**Default:** `1000000` (100 gold) · **Values:** non-negative integer (`0` for free) · **Restart:** needed (requires two restarts)

<details>
<summary>How it works in detail</summary>

Turn-in fee required to complete unlock quests for Dire Maul Tribute, Sayge's Fortune, Songflower Serenade, and Traces of Silithyst.

Fees are written into the quest templates at server startup. Modifying this setting requires two full server restarts before the updated gold cost appears in the quest dialogue.

</details>
