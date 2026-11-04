---
id: play-known-limitations
title: Known Limitations & Quirks
category: play
summary: What bots cannot do yet, what is left out on purpose, and quirks you may notice while playing.
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
- **Founding guilds:** bots are meant to buy guild charters and found their own guilds ([`AiPlayerbot.RandomBotFormGuild`](../server/settings/world-population.md#aiplayerbotrandombotformguild), on), but on test realms with 1000 bots none did. Invite pool bots to your own guild instead ([Guilds](../world/social.md#guilds)).
- **Pre-Movement Loot Admission:** Bots check loot rights for creature corpses before walking to them. However, the server engine currently lacks an upfront query for game object loot rights; a bot may occasionally walk toward a gathered herb or mined vein before determining that there is nothing lootable.

<a id="opt-in-dungeon-clearing"></a>
## Dungeon run leader (work in progress)

The run leader (`.bot run`) lets your party walk a stored dungeon route by itself: it pulls, fights, rests, loots and opens doors. It is switched on by default ([`AiPlayerbot.DungeonSelfClear`](../server/settings/dungeons-raids.md#aiplayerbotdungeonselfclear) = `1`), but it is still being built and is not officially supported yet. See [Dungeon Run Leader](dungeon-run-leader.md). What to expect:

- **It needs a stored route.** The server ships routes for 46 dungeons and raids, but most are drafts that have never been walked. Uldaman and Zul'Farrak have been checked in a live run; Ragefire Chasm and Wailing Caverns are mapped but not live-checked. Expect the other routes to get stuck.
- **It runs your own party.** You lead the party, bind it with `.bot commander bind <LeadBot> inparty` (the lead bot becomes the party leader) and start the run with `.bot run start`; you stay in the party. Running a bot-only party from outside needs [`AiPlayerbot.RemoteCommander`](../server/settings/performance.md#aiplayerbotremotecommander) = `1` (off by default). Letting you walk in front while the bots pull near you (`.bot run start ... player`) needs `AiPlayerbot.DungeonRunPlayerLed` = `1` (off by default) and has not been proven in a live run.
- **You stay in control.** Any order you give the bound bots pauses the run; you can pause, skip, resume or stop it. Every wait has a time limit, and when it runs out the run pauses and tells you why.
- **Some route steps are not supported yet:** `skip`, per-leg `rest`, `companions_loot`, `area_trigger` and gossip steps.
- **Hazard areas are not known to the run.** A fight, a knockback or a pull can still drag the party into a spot the route marks to avoid (for example the lava in Ragefire Chasm).

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
| **Upkeep Buff Mana Floor** | Out of combat, bots do not recast upkeep buffs when the cast would leave them below the upkeep floor: pool bots 40% (70% for charged shields); companion bots with a master 20% (40% for charged shields). | Let bots drink to full; buffing resumes automatically once mana recovers ([Combat settings](../server/settings/combat.md)). |
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
