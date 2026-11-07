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

- **Playing your own character as a bot:** Not possible. Bots are secondary characters on your account (headless logins) or hired mercenaries. You cannot turn the character you are playing into a bot.
- **Looting herbs and ore:** For creature corpses, bots check loot rights before walking over. For herbs and veins there is no such upfront check: a bot may walk to a node and only find out at the node that there is nothing to loot.

<a id="opt-in-dungeon-clearing"></a>
## Dungeon run leader (work in progress)

The run leader (`.bot run`) lets your party walk a stored dungeon route by itself: it pulls, fights, rests, loots and opens doors. It is switched on by default ([`AiPlayerbot.DungeonSelfClear`](../server/settings/dungeons-raids.md#aiplayerbotdungeonselfclear) = `1`), but it is still being built and is not officially supported yet. See [Dungeon Run Leader](dungeon-run-leader.md). What to expect:

- **It needs a stored route.** The server ships 46 routes for dungeons and raids, but most are drafts that have never been walked. Uldaman and Zul'Farrak have been checked in a live run; Ragefire Chasm and Wailing Caverns are mapped but not live-checked. Expect the other routes to get stuck.
- **It runs your own party.** You lead the party and stay in it.
  - Bind with `.bot commander bind <LeadBot> inparty` (the lead bot becomes the party leader), then start with `.bot run start`.
  - A bot-only party from outside needs [`AiPlayerbot.RemoteCommander`](../server/settings/performance.md#aiplayerbotremotecommander) = `1` (off by default).
  - Walking in front while bots pull near you (`.bot run start ... player`) needs `AiPlayerbot.DungeonRunPlayerLed` = `1` (on by default) and has not been proven in a live run.
- **You stay in control.** Any order you give the bound bots pauses a bot-led run; you can pause, skip, resume or stop it. Every wait has a time limit, and when it runs out the run pauses and tells you why.
- **Some route steps are not supported yet:** `skip`, per-leg `rest`, `companions_loot`, and `area_trigger` steps. Gossip steps work (the run talks to the NPC for you).
- **Hazard areas are not known to the run.** A fight, a knockback or a pull can still drag the party into a spot the route marks to avoid (for example the lava in Ragefire Chasm).

## Explicit Non-Goals

TortoiseBots deliberately excludes certain features to maintain immersion, stability, and vanilla fidelity:

- **Expansion content:** No Death Knights, Outland/Northrend zones, or Arena ratings. Classes and mechanics stick to Vanilla 1.12 with Turtle WoW 1.18.1 additions.
- **Bot chatter:** Bots do not chat on their own: no replies to what players say, no announcements of loot, quests, kills or level-ups, no suggestions in public channels and no language-model chat. They answer orders and questions from their own master and group, and a bot LFG group still advertises in LookingForGroup. Conversation between bots and players is left to a separate chat module.
- **Multi-tab guild vaults:** Bot interactions with unsupported guild bank actions fail closed safely.

## Tactical & Targeting Rules for Players

When adventuring with bots, understanding their targeting rules helps avoid unexpected pulls (initiating combat):

- **Pull level caps:** Pool bots on their own (no master), outside battlegrounds: below level 10 they only pull mobs up to 1 level above their own level. From level 10 upward, they can engage enemies up to 4 levels higher.
- **Pull health and mana thresholds:** Pool bots on their own, outside battlegrounds, will not start a new pull while hurt (health, or mana for mana users, below [`AiPlayerbot.MediumHealth`](../server/settings/combat.md#aiplayerbotmediumhealth) / [`AiPlayerbot.MediumMana`](../server/settings/combat.md#aiplayerbotmediummana)). An attacker that already holds the bot as victim is always answered. Healers without off-DPS start no fights on their own.
- **Runner finishing policy:** When an enemy of the party's fight flees below 30% health, bots finish the runner according to [shared runner rules](../classes/overview.md#shared-bot-behaviors). In a dungeon run, bots do not follow a runner down a cliff (more than 6 yards below, with a cliff or no complete path on the way); a runner let go there may come back with help.
- **Auto-attack engagement:** Bots arm the melee auto-attack when ordered to attack (healers excepted), so they do not stand idle when out of mana or within melee distance.

## Known Quirks and Workarounds

For deep technical details on these behaviors, see [Bot Mechanics & Quirks](../developers/bot-mechanics.md).

| Quirk / Situation | What Happens | What You Should Do |
| :--- | :--- | :--- |
| **Stairs and line of sight** | On steep or multi-tiered stairs, pathfinding can hitch when line of sight is lost. | Use `.bot action come` or [`.bot summon <Name>`](commands/roster-and-hiring.md#bot-summon) to snap bots to your floor. |
| **Warlock Life Tap** | Warlocks stop tapping at or below the low-health line (`AiPlayerbot.LowHealth`, 50%), but burst damage can still catch a low bot before heals land. | React sooner: [`.bot` low-health setting](../server/settings/combat.md#aiplayerbotlowhealth) = 65. |
| **Shaman totems** | Moving to a new fight, Shamans may re-drop totems at the new spot, leaving old ones behind. | Normal behavior; mana recovers while drinking out of combat. |
| **Buff mana floor** | Out of combat, bots skip upkeep buffs when the cast would leave them below the floor: pool bots 40% (70% for charged shields); bots with a master 20% (40% for charged shields). | Let bots drink to full; buffing resumes on its own ([Combat settings](../server/settings/combat.md)). |
| **Starter graveyard rescue** | Bots below level 10 avoid the spirit healer and relocate when revived into an above-level area. Corpse runs stay the default unless the ghost stalls. | Normal safety behavior for starter bots. |
| **Group buffs** | Group buffs go out only when at least 3 party members lack both versions. | Normal behavior; single-target buffs cover the rest. |
| **Buff line of sight** | Bots prefer a visible member for buffs, cures, and resurrection. | Step into the open so bots can see you. |
| **Double buffs** | Bots coordinate a short claim window so two bots do not cast the same buff. | Normal behavior; nothing to do. |
| **Hidden GMs** | A hidden-GM party member is skipped by buffs, cures, and resurrection. | Run `.gm visible on` when testing on a GM character. |
| **Loot timing** | Bots check combat before opening corpses, and inside dungeons skip corpses near unpulled packs (kept queued up to 3 minutes). | Clear the pack; bots loot once combat ends. |
| **Evading enemies** | When an enemy evades and runs home, bots drop it at once. | Wait for it to reach its spawn and stop before re-engaging. |
| **Ghost stalls** | If a masterless ghost makes no progress toward its corpse for about a minute, it takes the spirit healer. A companion ghost never auto-releases: revive it yourself or wait for it to take the healer. A dead bot cannot be summoned until it is alive again. | Revive a stuck companion ghost, or wait for the spirit healer. |
| **Gathering** | Gathering needs standing still; follow-up and loot chains pause while the cast runs. | Stand by the node until the bot finishes. |

<details>
<summary>How companion travel and instance safety are isolated</summary>

Companion bots under your command stand down travel and the automatic gathering sweep inside dungeons and raids. Outside instances nothing changes. Inside, they only gather a node they stand right next to, out of combat, and only an explicit loot order gathers while hostiles are near.

</details>
