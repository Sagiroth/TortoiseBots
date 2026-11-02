---
id: play-known-limitations
title: Known Limitations & Quirks
category: play
summary: Authoritative list of current engine boundaries, blocked features, explicit non-goals, and gameplay quirks.
tags: [limitations, non-goals, quirks, engine, roadmap]
relates_to:
  - play-getting-started
  - play-dungeons-and-raids
  - server-bot-pool
---

# Known Limitations & Quirks

TortoiseBots strives to make companion bots behave like human players. However, certain technical boundaries, design trade-offs, and behavioral quirks exist. This page provides an authoritative summary of what bots cannot do yet, deliberate non-goals, and edge cases you may notice while playing.

## Blocked & Incomplete Features

- **Self-Botting (Playing Your Own Character as a Bot):** You cannot convert your currently logged-in character into an automated bot. No network-session attach seam exists in the core server; converting a network session into a headless session mid-connection would violate session integrity. Bots must be secondary characters logged in as headless sessions from your account or hired mercenaries.
- **Synthetic Auction Market:** While bots list and purchase items from their actual inventories, fully synthetic auction supply and synthetic buyers are optional features disabled by default ([`AiPlayerbot.AhMarketSyntheticSupply`](../server/settings/economy.md#aiplayerbotahmarketsyntheticsupply) and [`AiPlayerbot.AhMarketBuyer`](../server/settings/economy.md#aiplayerbotahmarketbuyer)). When enabled, synthetic items remain isolated from real player inventories.
- **Pre-Movement Loot Admission:** Bots check loot rights for creature corpses before walking to them. However, the server engine currently lacks an upfront query for game object loot rights; a bot may occasionally walk toward a gathered herb or mined vein before determining that there is nothing lootable.

## Opt-In Dungeon Clearing

By default, 5-player dungeons and raids are cleared by a player (or a remote commander) directing companion bots; the run leader that walks a stored dungeon plan by itself is on by default (`AiPlayerbot.DungeonSelfClear = 1`), started with an `inparty` bind or a player-led run in the player's own party. The remote commander drives parties from outside. The **run leader** (`.bot run`) is described in [Dungeon Run Leader](dungeon-run-leader.md), and its setting in [Dungeon & raid settings](../server/settings/dungeons-raids.md#aiplayerbotdungeonselfclear). Its limits:

- **A stored plan is needed:** a run follows a dungeon plan from the world tables (`ai_dungeon_plan`, `ai_dungeon_leg`, `ai_dungeon_step`). Plans are on the server for Uldaman, Zul'Farrak, Ragefire Chasm and Wailing Caverns, and for Deadmines, whose plan is still a draft in turtle-agent. No raid plan is listed.
- **Bound parties only:** the run leads a bound party (`.bot commander bind`, with `inparty` for your own party). From 2026-10-23 a bind with `inparty` kept 1 to 4 real players (the commander among them) in the party, the lead bot leading; a remote bind (needs `AiPlayerbot.RemoteCommander = 1`) runs a bot-only party from outside. From 2026-10-24 a player-led run (`.bot run start ... player`, the player walks and the run pulls near him, [Dungeon Run Leader](dungeon-run-leader.md#player-led-runs)) was built in slices S1-S4 behind `AiPlayerbot.DungeonRunPlayerLed` (default 0), and S5-S6 (player down and away, key-item loot, object and event steps near the player), with no live proof yet; readable chat lines beside the `TBM:` ones were not built.
- **The commander stays in control:** the commander (a player, or turtle-agent in observe mode) watches the run and may pause, skip, resume or stop it. Any commander order pauses it (another player's orders to bound bots are refused; party chat and normal play never pause it). Every wait has a time limit, and a ceiling pauses the run with the reason.
- **Some plan steps are not carried over yet:** plan v1's `skip`, per-leg `rest`, `companions_loot`, `area_trigger` and gossip.
- **Avoid boxes are not known to the run:** its hops are the server's navmesh paths, so a fight, a knockback or a pull onto a plan's avoid box (Ragefire Chasm's lava basin) is not covered.
- **Little live evidence:** Wailing Caverns' ramp to Lord Serpentis is a `move_direct` (a straight walk down over a 3.9 yd gap). It was walked in one live observe clear of the whole plan (2026-10-20, a candidate result). The other dungeons have had partial runs.

The separate `mod-dungeon-clear` module is not ported as a second autonomous pull and pathing brain; the run leader is TortoiseBots' own.

## Explicit Non-Goals

TortoiseBots deliberately excludes certain features to maintain immersion, stability, and vanilla fidelity:

- **Expansion Content (TBC / WotLK):** No Death Knights, glyphs, vehicles, Outland/Northrend zones, or Arena ratings. Classes and mechanics strictly adhere to Vanilla 1.12 with Turtle WoW 1.18.1 additions.
- **Bot chatter:** Bots do not chat on their own: no replies to what players say, no announcements of loot, quests, kills or level-ups, no suggestions in public channels and no language-model chat. They answer orders and questions from their own master and group, and a bot LFG group still advertises in LookingForGroup. Conversation between bots and players is left to a separate chat module.
- **Multi-Tab Guild Vaults:** Turtle WoW 1.18.1 does not support WotLK-style multi-tab guild banks. Bot interactions with unsupported guild bank actions fail closed safely.

## Tactical & Targeting Rules for Players

When adventuring with bots, understanding their targeting rules helps avoid unexpected pulls (initiating combat):

- **Pull Level Caps:** Autonomous pool bots below level 10 outside battlegrounds will only pull mobs up to 1 level above their own level. From level 10 upward, they can engage enemies up to 4 levels higher.
- **Pull Health and Mana Thresholds:** Masterless pool bots outside battlegrounds will not initiate a new pull while wounded (health, or mana for mana users, below [`AiPlayerbot.MediumHealth`](../server/settings/combat.md#aiplayerbotmediumhealth) / [`AiPlayerbot.MediumMana`](../server/settings/combat.md#aiplayerbotmediummana)); revenge against an attacker that already holds the bot as victim is always answered. Healers without off-DPS start no fights on their own.
- **Runner Finishing Policy:** When an enemy of the party's fight flees below 30% health, bots finish the runner according to [shared runner rules](../classes/overview.md#shared-bot-behaviors) to avoid pulling adjacent packs. In a dungeon run, bots do not follow a runner down a cliff (more than 6 yards below, with a cliff or no complete path on the way); a runner let go there may come back with help.
- **Auto-Attack Engagement:** Bots arm the melee auto-attack when ordered to attack (healers excepted, and only for a bot with no usable ranged weapon when the strategy set calls it ranged), ensuring they do not stand idle when out of mana or within melee distance.

## Known Quirks and Workarounds

For deep technical details on these behaviors, see [Bot Mechanics & Quirks](../developers/bot-mechanics.md).

| Quirk / Situation | What Happens | What You Should Do |
| :--- | :--- | :--- |
| **Line-of-Sight on Stairs** | On steep or multi-tiered stairs (e.g. Blackrock Depths), pathfinding can occasionally hitch when line-of-sight is lost. | Use `.bot action come` or [`.bot summon <Name>`](commands/roster-and-hiring.md#bot-summon) to snap bots to your floor. |
| **Warlock Life Tap Risk** | Warlocks stop tapping at or below the low-health line (`AiPlayerbot.LowHealth`, 50%), but incoming burst damage can catch a low bot before heals land. | Set healer reaction urgency higher ([`AiPlayerbot.LowHealth`](../server/settings/combat.md#aiplayerbotlowhealth) = 65). |
| **Shaman Totem Churn** | When moving between combat anchors, Shamans occasionally refresh totems, leaving old ones behind and re-dropping them at new fight locations. | Normal behavior; Shaman mana recovers during out-of-combat drinking. |
| **Upkeep Buff Mana Floor** | Out of combat, bots do not recast upkeep buffs when the cast would leave them below the upkeep floor: pool bots 40% (70% charged shields); companion bots with a master 20% (40% charged, per GroupBuffPolicy.h). | Let bots drink to full; buffing resumes automatically once mana recovers ([Combat settings](../server/settings/combat.md)). |
| **Lowbie Graveyard Rescue** | Pool bots below level 10 never take the spirit healer just for the death count, and relocate safely when revived or teleported into an above-level area. Corpse runs stay the default unless the ghost stalls or lies dead a long time. | Normal safety mechanism to prevent starter bots from getting permanently trapped. |
| **Greater Group Buffs** | Group buffs are cast only when at least 3 party members lack both the group and single-target versions. | Normal behavior; single-target buffs cover solo missing party members. |
| **Target Out of Line of Sight** | Bots prefer a visible party member for buffs, cures, and resurrection; a member out of sight is only taken when no matching member is in sight. | Step into open line of sight so bots can see you to cast buffs. |
| **Duplicate Buff Protection** | When multiple bots can cast the same buff, they coordinate a 4-second claim window to prevent double-casting. | Normal behavior; each member is covered once without wasting mana. |
| **Hidden GM Group Members** | A party member who is a hidden GM is intentionally skipped by bot party-member searches (buffs, cures, resurrection). | Run `.gm visible on` if testing companion bot spells on a GM character. |
| **Loot Timing vs Next Pull** | Bots check live combat state before opening corpses to avoid kneeling down next to incoming adds. | Clear the pack; bots will loot remaining corpses once combat ends. |
| **Loot Near Unpulled Packs** | Inside dungeons, bots skip looting corpses that lie within sight range of unpulled hostile packs. | Clear surrounding packs; skipped corpses remain queued for up to 3 minutes. |
| **Evading Enemies** | When an enemy enters evade mode (or becomes unreachable) and runs home, bots stop picking and drop it from targeting immediately; its damage is refused and it regenerates while returning. | Wait for the mob to reach its spawn point and stop moving before re-engaging. |
| **Ghost Corpse Run Stalls** | If a masterless ghost stops closing the distance to its corpse (about a minute without gaining five yards), it hands off to the spirit healer. Inside the corpse reclaim radius it revives without walking. | If a companion ghost is stuck, revive it or wait for it to take the spirit healer; a dead bot cannot be summoned until it resurrects. |
| **Gather Cast Interruption** | Gathering nodes requires standing still; follow station-keeping and the loot chain stand down while the gather cast runs. | Stand next to the gathering node until the bot finishes harvesting. |

<details>
<summary>How companion travel and instance safety are isolated</summary>

Companion bots under your command (a bot with a real player master) have travel selection and travel movement stood down inside dungeons and raids. Outside instances this changes nothing; the stand-down applies inside dungeons and raids only.

Inside dungeons and raids, companion bots also stand down the automatic gathering sweep: the node is still refused for hostiles nearby, and only an explicit loot order gathers inside. They only gather nodes when standing immediately beside them out of combat.

</details>
