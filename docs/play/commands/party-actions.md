---
id: play-commands-party-actions
title: Tactical Party Actions
category: play
summary: Tactical party actions, live combat toggles, and direct movement commands for player-led parties.
tags: [commands, tactics, party-actions]
relates_to:
  - play-commands
  - play-commands-commander
  - server-settings-party
---

# Tactical Party Actions

TortoiseBots keeps orders simple: instead of micromanaging abilities, you issue high-level orders and the bots carry them out with their normal rotations.

Tactical party actions require you to be alive and in the world, and you must control the bots (account owner, group master, claimed bot, or GameMaster). If an action cannot be parsed, the error names the first word typed (up to 32 bytes, UTF-8 safe, `|`/line-breaks → spaces, `unknown` if empty) with code `invalid`.

## Dungeon Movement Mechanics

Companion bots follow special navigation rules inside dungeons and raids to avoid pathing deadlocks:

- **Ranged Reach and Line of Sight (LoS):** Ranged damage dealers and healers walk the navigation mesh to a point that has line of sight to their target within cast range. They do not run into melee range when casting spells.
- **Platform Step-Down:** When a bot is stuck on a raised ledge or platform with no ramp, it checks for walkable ground 2 to 8 yards lower and steps down directly without getting stuck.
- **Follow Catch-Up:** Outside of combat, companion bots trailing more than 3 yards behind their follow distance walk the navmesh back to their follow slot near the leader once a 2-second stand-still is seen. When no walk can start, the bot waits before retrying and gives up after repeated failures.

## Summary

| Command | Who | Target Required | What It Does |
| :--- | :--- | :--- | :--- |
| [`.bot action attack`](#bot-action-attack) | Controller | Hostile Target | Orders scoped damage and tank bots to engage target. |
| [`.bot action interrupt`](#bot-action-interrupt) | Controller | Casting Hostile | Orders the first bot with a ready interrupt to cast. |
| [`.bot action stop`](#bot-action-stop) | Controller | None | Stops attacks, keeps current movement. |
| [`.bot action flee`](#bot-action-flee) | Controller | None | Orders bots to break combat and follow passively. |
| [`.bot action pull`](#bot-action-pull) | Controller | Hostile Target | Sends tank to pull while party waits to engage. |
| [`.bot action pullback`](#bot-action-pullback) | Controller | Hostile Target | Tank pulls and retreats to your anchor position. |
| [`.bot action come`](#bot-action-come) | Controller | None | Commands scoped bots to sprint to your location. |
| [`.bot action stay`](#bot-action-stay) | Controller | None | Halts scoped bots at their current positions. |
| [`.bot action follow`](#bot-action-follow) | Controller | None | Resumes tight follow formation behind the leader. |
| [`.bot action focus skull`](#bot-action-focus-skull) | Controller | Hostile Target / None | Sets Skull mark (or uses existing) and orders attack. |
| [`.bot action cc`](#bot-action-cc) | Controller | Target / None | Assigns crowd control raid mark to a capable bot. |
| [`.bot action cc clear`](#bot-action-cc-clear) | Controller | Bot / None | Clears crowd control mark assignments. |
| [`.bot action auto cc`](#bot-action-auto-cc) | Controller | None | Toggles autonomous crowd control on loose adds. |
| [`.bot action sap`](#bot-action-sap) | Controller | Rogue / None | Toggles pre-pull Sap behavior on CC marks. |
| [`.bot action aoe`](#bot-action-aoe) | Controller | None | Toggles area-of-effect spells across party bots. |
| [`.bot action loot`](#bot-action-loot) | Controller | None | Toggles corpse looting on or off. |
| [`.bot action repair`](#bot-action-repair) | Controller | None | Orders bots to repair gear at a nearby vendor. |
| [`.bot action sell`](#bot-action-sell) | Controller | None | Orders bots to sell gray junk items to vendor. |
| [`.bot action rest`](#bot-action-rest) | Controller | None | Orders bots to sit and consume food and drink. |
| [`.bot action release`](#bot-action-release) | Controller | None | Commands dead companion bots to release spirit. |
| [`.bot action corpse run`](#bot-action-corpse-run) | Controller | None | Commands ghost bots to run back to their corpse. |
| [`.bot action learn`](#bot-action-learn) | Controller | None | Orders bots to learn spells from nearby trainers. |
| [`.bot action trade`](#bot-action-trade) | Controller | Companion Bot | Opens a direct trade window with targeted bot. |
| [`.bot action ready`](#bot-action-ready) | Controller | None | Initiates a group readiness check across all bots. |
| [`.bot action end pull`](#bot-action-end-pull) | Controller | None | Aborts a running pull maneuver. |
| [`.bot action stance`](#bot-action-stance) | Controller | None | Sets per-bot combat facing. |
| [`.bot action rebuff`](#bot-action-rebuff) | Controller | None | Refreshes expiring party buffs out of combat. |
| [`.bot action guard`](#bot-action-guard) | Controller | None | Holds ground at each bot's current spot. |
| [`.bot action free`](#bot-action-free) | Controller | None | Adds autonomous `free` movement on scoped bots. |
| [`.bot action raid status`](#bot-action-raid-status) | Controller | None | Displays active raid strategies for party bots. |
| [`.bot action raid tankface`](#bot-action-raid-tankface) | Controller | Target | Drags the target away from the raid anchor. |
| [`.bot action raid douse`](#bot-action-raid-douse) | Controller | None | Orders bots to douse nearby Molten Core runes. |
| [`.bot action raid custom status`](#bot-action-raid-custom-status) | Controller | None | Reports custom Turtle raid strategy status. |
| [`.bot action raid custom on`](#bot-action-raid-custom-on) | Controller | None | Enables custom Turtle raid transition strategies. |
| [`.bot action raid custom off`](#bot-action-raid-custom-off) | Controller | None | Disables custom Turtle raid transition strategies. |
| [`.bot follow`](#bot-follow) | Controller | Bot (Required) | Orders a specific bot to follow you. |
| [`.bot stay`](#bot-stay) | Controller | Bot (Required) | Orders a specific bot to hold position. |
| [`.bot guard`](#bot-guard) | Controller | Bot (Required) | Orders one bot to defend its current ground. |
| [`.bot free`](#bot-free) | Controller | Bot (Required) | Releases a bot to autonomous free movement. |
| [`.bot ready`](#bot-ready) | Controller | Bot (Required) | Checks readiness for one specific bot. |
| [`.bot attack`](#bot-attack) | Controller | Bot (Required) | Orders one specific bot to attack your target. |
| [`.bot interrupt`](#bot-interrupt) | Controller | Bot (Required) | Orders one specific bot to interrupt a cast. |
| [`.bot pullback`](#bot-pullback) | Controller | Live Target | Dispatches pullback maneuver on designated tank. |
| [`.bot formation`](#bot-formation) | Controller | None / Name | Sets party follow formation spacing and pattern. |
| [`.bot strategy`](#bot-strategy) | Controller | Strategy Name | Adds or removes combat and non-combat strategies. |
| [`.bot loot`](#bot-loot) | Controller | None / on / off | Toggles corpse looting on targeted bot or party. |
| [`.bot repair`](#bot-repair) | Controller | Bot / None | Orders targeted bot or party to repair gear. |
| [`.bot sell`](#bot-sell) | Controller | Bot / None | Orders targeted bot or party to sell gray junk. |
| [`.bot rest`](#bot-rest) | Controller | Bot / None | Orders bots to sit and consume food and drink. |
| [`.bot downtime`](#bot-downtime) | Controller | Bot / None | Orders own alt bots to run town errands (trainer, junk, repair). |

---

## Action Reference

### `.bot action attack`

Engages your hostile target with scoped tanks and DPS bots (selected bot if targeted, else party).

```text
.bot action attack
```

- Range: accepts targets out to `AiPlayerbot.ReactDistance` (150 yards by default, follows config).
- Healers keep healing instead of attacking.
- Evade: creatures in evade mode trigger a `"<name> is evading"` whisper.
- Unreachable: lifts temp unreachable flags on the target and its mob entry.
- Still unreachable after 15 seconds: bots drop the target and ignore that mob entry for five minutes.
- Refusals: `dead-target`, `no-target`.
- Releases stay and flee holds on the dispatched bots.

### `.bot action interrupt`

Interrupts your target's spell cast with the first party bot that has a ready interrupt.

```text
.bot action interrupt
```

- If you target one of your bots, only that bot tries.
- Spells: *Counterspell*, *Silence*, *Spell Lock*, *Kick*, *Pummel*, *Shield Bash*, *Earth Shock*, *Bash*, *Hammer of Justice*, *Repentance*, *Death Coil*.
- Bots past sight distance (60 yards) are skipped.
- Refusals: `no-target`, `dead-target`, `not-casting`, `no-interrupt`.

### `.bot action stop`

Stops attacks on scoped bots (selected bot if targeted, else party) without changing movement.

```text
.bot action stop
```

- Keeps the current movement mode: a bot on stay stays where it is.
- Does not release the DPS join window of a running pull.

### `.bot action flee`

Disengages scoped bots (selected bot if targeted, else party) into passive follow.

```text
.bot action flee
```

- Drops combat targets and pull holds.
- Ends when the next order sends bots at a target or changes movement.

### `.bot action pull`

Pulls your hostile target with the party tank while other bots hold at your position.

```text
.bot action pull [seconds] [ranged|body]
```

- `[seconds]` (0 to 60, default 10) and `[ranged]` or `[body]` in either order; `ranged` and `body` exclude each other.
- Others hold at your position for the delay after the tank engages, then join combat.
- The tank's rotation takes over; no second order needed.
- The join window runs on its own clock.
- `attack` and `flee` end the window early; combat end, death, or movement orders only release the anchor hold.
- A plain pull walks into melee unless every ranged-opening condition holds.
- `ranged` walks to a firing position first; `pullback` always opens from range.
- `body` walks in and pulls in melee whatever the mob is, over the conditions below.
- If the tank cannot shoot, it walks in instead.

Ranged-opening conditions (all must hold):

- Ranged pulling is on (`AiPlayerbot.RangedPull`, default on).
- The target is in line of sight and inside the shot's range.
- The target is unreachable on foot, or farther than `AiPlayerbot.RangedPullMeleeApproach` (default 20 yards).

| Blocker | Meaning |
| :--- | :--- |
| `no-ranged-weapon` | No ranged weapon equipped |
| `weapon-untrained` | Weapon skill missing |
| `shoot-spell-missing` | Shoot spell not learned |
| `no-ammo` | No ammo |
| `too-close` | Target inside the shot's dead zone |

- The reply names the blocker.
- It is also whispered when you ordered `ranged`, or when the tank has a ranged weapon it cannot fire.

| Refusal | Meaning |
| :--- | :--- |
| `dead-target` | Target is dead |
| `friendly` | Target is friendly |
| `too-far` | Past `ReactDistance * 3` (450 yards at defaults) |
| `unreachable` | No foot path for a ranged walk |
| `invalid-target` | Not a valid target |
| `no-tank` | No tank available |
| `tank-busy` | Only tanks found are already fighting |

### `.bot action pullback`

Pulls the target from range and retreats to your position, breaking line of sight around corners.

```text
.bot action pullback [seconds] [ranged|body]
```

- `[seconds]` (0 to 60, default 5) and `[ranged]` or `[body]` in either order; `ranged` and `body` exclude each other.
- The tank shoots, then returns to where you stand.
- `body` makes it walk in and pull in melee first, then return the same way.
- Others attack once the tank is back and the join delay stamped at the shot has run out.
- No working ranged weapon: the tank strikes once in melee and sprints back.
- The tank holds at your position for the fight the pull brought in.
- It fights freely and follows again after combat.
- Eating or drinking tanks are interrupted and stood up first.

### `.bot action come`

Walks scoped bots (selected bot if targeted, else party) to your exact spot and switches them to `stay`.

```text
.bot action come
```

- Also: `.bot action hold`, `.bot action comestay`.
- Only `come` records a come order (lets a pending summon follow-restore run again).
- `hold` and `comestay` record a hold order instead.

### `.bot action stay`

Halts scoped bots (selected bot if targeted, else party) where they stand.

```text
.bot action stay
```

- Targeted combat orders release dispatched bots so they engage and follow after combat.

### `.bot action follow`

Resumes follow formation behind you on scoped bots (selected bot if targeted, else party).

```text
.bot action follow
```

- If `AiPlayerbot.DungeonFollowHoldBeforePacks` is on, a trailing bot in a dungeon or raid holds outside unpulled packs and whispers you a warning.
- It resumes when the pack dies or you return.
- The hold opens past 30 yards and closes inside 25 yards.
- When you step through a party mage's city portal, bots that follow you walk to it and step through too, the mage last. A bot that misses the portal before it closes is brought to you.

### `.bot action focus skull`

Attacks the Skull mark with all scoped party bots, using normal rotations.

```text
.bot action focus skull
```

- Applies Skull to your hostile target, or uses the existing Skull mark if you have no target.
- Healers are not exempted here.

<a id="bot-action-cc"></a><a id="bot-cc"></a>
### `.bot action cc`

Assigns a crowd-control raid mark to a capable bot.

```text
.bot action cc <mark> [bot]
```

- Marks: `moon`, `star`, `circle`, `diamond`, `triangle`, `square`, `cross`, `skull`.
- `skull` is normally reserved for focus fire.
- The server probes each bot's crowd-control actions and picks the best fit.
- Probed spells include *Polymorph*, *Sap*, *Banish*, *Fear*, *Shackle Undead*, *Hibernate*, *Entangling Roots*, *Freezing Trap*, *Scare Beast*, *Turn Undead*.
- One bot owns a mark at a time; reassigning clears the previous owner.
- Bots avoid area spells near controlled targets.
- Assigned rogues stealth and Sap suitable targets before the pull by default.

### `.bot action cc clear`

Clears crowd-control mark assignments.

```text
.bot action cc clear [bot]
```

- A bot name clears only that bot; targeting an owned bot clears that bot.
- Neither clears all party bots.
- `.bot cc <mark> [bot]` and `.bot cc clear [bot]` work the same way.

### `.bot action auto cc`

Toggles autonomous crowd control on loose hostile adds.

```text
.bot action auto cc [on|off|status]
```

- No argument flips the setting.
- Fires in group combat with 2 or more enemies.
- Target must be a hostile hitting a healer or ranged bot, never the tank.
- Nobody in the group may be attacking or targeting it.
- Skipped: targets with periodic-damage auras, the last mob alive, Skull-marked targets, the tank's victim.
- One bot per mob; never re-sheeps active control; explicit marks win.

### `.bot action sap`

Toggles pre-pull *Sap* on crowd-control marks.

```text
.bot action sap [on|off] [bot]
```

- On by default.
- A bot name or targeted bot scopes to that bot, else all party rogues.
- Non-rogues are skipped; no rogues in scope returns `no-rogue`.
- Replies `on`, `off`, or `mixed`.
- `sap=on|off` also rides the `TBM:BOTSTATE` line per bot.

### `.bot action aoe`

Toggles area-of-effect spells on scoped bots.

```text
.bot action aoe [on|off|status]
```

- Bare `aoe` flips the switch; `status` reports it.
- On by default.
- Off applies the `no aoe` strategy (shared with `.bot behavior <bot> aoe`, saved across sessions).
- Off also stops the `dps aoe` picker from picking cluster targets.

| Class | Spells held back while off |
| :--- | :--- |
| Mage | Arcane Explosion, Blast Wave, Blizzard, Cone of Cold, Flamestrike, Frost Nova |
| Warlock | Rain of Fire, Howl of Terror |
| Priest | Holy Nova, Psychic Scream |
| Druid | Hurricane, Swipe, Swipe (bear), Demoralizing Roar, Challenging Roar |
| Hunter | Multi-Shot, Volley, Carve, Explosive Trap, Explosive Trap on target, Frost Trap, Frost Trap on target |
| Paladin | Consecration, Holy Wrath |
| Shaman | Chain Lightning, Magma Totem, Fire Nova, Earthquake |
| Warrior | Cleave, Whirlwind, Sweeping Strikes, Thunder Clap, Thunder Clap on snare target, Demoralizing Shout, Challenging Shout, Intimidating Shout, Piercing Howl |
| Rogue | Blade Flurry |
| Other | War Stomp, Goblin Sapper, throw grenade, Oil of Immolation |

### `.bot action loot`

Toggles corpse looting on scoped bots.

```text
.bot action loot [on|off|status]
```

- On by default.
- No argument flips the setting and reports the result.

### `.bot action repair`

Repairs scoped bots' gear at a nearby repair merchant.

```text
.bot action repair
```

### `.bot action sell`

Sells scoped bots' gray junk at a nearby stocked vendor.

```text
.bot action sell
```

- If nothing sells, the reply says why.
- Reasons: nothing grey to sell, no vendor in reach, or no grey item the bot will sell here.

### `.bot action rest`

Sits scoped bots down to eat and drink until health and mana are full.

```text
.bot action rest
```

- Also: `.bot action drink`, `.bot action eat`.
- Mage bots share conjured food and water with the party.
- In a dungeon run, no rest while any bound bot or its pet is fighting.
- That hold reports as `party still fighting`.
- If bots cannot rest, the reply groups reasons (`No bot rested: <reason>: <bots>...`).
- Reasons: `dead`, `in combat`, `party still fighting`, `no food or drink` (or `no drink`, `no food`), `leader is moving`, `nothing to rest (mana X% >= Y%, health A% >= B%)`, `other: not useful now`.

### `.bot action release`

Releases the spirit of dead scoped bots.

```text
.bot action release
```

- See [Roster and Hiring Commands: .bot release](roster-and-hiring.md#bot-release).

### `.bot action corpse run`

Runs scoped ghost bots back to their corpses.

```text
.bot action corpse run
```

- Alias: `.bot action corpserun`.
- See [Roster and Hiring Commands: .bot corpse run](roster-and-hiring.md#bot-corpse-run).

### `.bot action learn`

Learns available spells and ranks from the selected trainer on scoped bots.

```text
.bot action learn
```

- Target a trainer you have selected.
- See [Roster and Hiring Commands: .bot learn](roster-and-hiring.md#bot-learn).

### `.bot action trade`

Opens a direct player-to-bot trade window with your targeted bot.

```text
.bot action trade
```

- See [Bot Panel: .bot item trade](bot-panel.md#bot-item-trade).

### `.bot action end pull`

Aborts a running pull maneuver on scoped bots.

```text
.bot action end pull
```

### `.bot action stance`

Sets combat facing on scoped bots.

```text
.bot action stance <near|tank|turnback|behind>
```

- `turn` works as `turnback`.

### `.bot action rebuff`

Refreshes expiring party buffs out of combat on scoped bots.

```text
.bot action rebuff
```

- Same 2-minute expiration pass as the whisper form.

### `.bot action guard`

Holds scoped bots at their current spots.

```text
.bot action guard
```

- Captures each bot's spot as its guard position.

### `.bot action free`

Adds autonomous `free` movement to scoped bots.

```text
.bot action free
```

- A `stay` hold drops out through the movement group.
- A `guard` hold stays with it, so a guarded bot keeps defending its spot.

### `.bot action ready`

Runs a readiness check across all scoped bots.

```text
.bot action ready
```

- Health above `AiPlayerbot.AlmostFullHealth` (90 by default).
- Mana above `AiPlayerbot.MediumHealth` (70) for mana users.
- Within `AiPlayerbot.SightDistance` (60 yards).
- Hunter ammo, pet presence, and pet happiness count too.
- Food, water, and potion counts count too.
- Each bot reports to you directly.
- Buff top-off happens only on real raid checks.
- It also needs `AiPlayerbot.ForceRebuffOnReadyCheck` on (default 0, off).
- And only bots with the `force rebuff` strategy top off.

### `.bot action raid status`

Shows the active raid encounter strategy for party bots.

```text
.bot action raid status
```

- Known strategies include `molten core`, `onyxia`, `four horseman`, `emerald sanctum`, `lower karazhan`, `karazhan crypt`, and `solnius`.
- Otherwise reports `instance map <id>, no raid strategy`, or `outdoor`.

### `.bot action raid tankface`

Drags the target away from the raid anchor so the boss head points away from the group.

```text
.bot action raid tankface
```

- Runs on any scoped bot with a live target; not limited to tanks or dragons.

### `.bot action raid douse`

Douses nearby Molten Core boss runes with scoped bots.

```text
.bot action raid douse
```

- Tries Eternal Quintessence first, then falls back to Aqual Quintessence.

### `.bot action raid custom status`

Reports the active state of custom Turtle WoW raid encounter strategies.

```text
.bot action raid custom status
```

- Outputs lowercase states: `emerald sanctum`, `lower karazhan`, `karazhan crypt`, or `off`.

### `.bot action raid custom on`

Enables custom raid encounter tactics for Turtle WoW raid instances.

```text
.bot action raid custom on
```

- Works regardless of configuration.
- `AiPlayerbot.EnableCustomRaidTactics` gates only automatic transition strategies on zone-in.

### `.bot action raid custom off`

Disables automated custom raid tactics, returning bots to standard manual control.

```text
.bot action raid custom off
```

---

## Direct Movement and Party Commands

Direct commands target one controlled bot by name (or the dynamic scope for toggles like loot and formation), while party-wide orders use `.bot action`.

### `.bot follow`

Orders an owned bot to follow you.

```text
.bot follow <Name>
```

- A bot name is required (without one, the server returns usage).
- For party-wide follow, use [`.bot action follow`](#bot-action-follow).

### `.bot stay`

Halts a specific bot at its current coordinates.

```text
.bot stay <Name>
```

- A bot name is required.
- For party-wide stay, use [`.bot action stay`](#bot-action-stay).

### `.bot guard`

Holds a specific bot at its current position to defend against nearby enemies.

```text
.bot guard <Name>
```

### `.bot free`

Releases an owned bot to autonomous `free` movement.

```text
.bot free <Name>
```

### `.bot ready`

Runs a readiness check on one named bot.

```text
.bot ready <Name>
```

- A bot name is required.
- Checks health, mana, distance, and supplies, and reports to you.
- For party-wide readiness, use [`.bot action ready`](#bot-action-ready).

### `.bot attack`

Orders a specific bot to attack your current hostile target.

```text
.bot attack <Name>
```

- Ends stay and flee holds on that bot.
- The bot follows you again when combat ends.

### `.bot interrupt`

Orders a specific bot to interrupt your current target's spell cast.

```text
.bot interrupt <Name>
```

### `.bot pullback`

Dispatches a pullback maneuver on your designated tank (or selected bot).

```text
.bot pullback
```

- Takes no name argument.
- Needs a live non-bot hostile target and a tank-capable bot.
- The tank pulls from range and retreats to your position.

### `.bot formation`

Configures the follow formation used by bots following the party leader.

```text
.bot formation [botName] <formation>
```

| Formation |
| :--- |
| `chaos` (shipped default `AiPlayerbot.DefaultFormation`; `default` resets to it) |
| `melee` |
| `queue` |
| `circle` |
| `line` |
| `shield` |
| `arrow` |
| `near` |
| `far` |

- One token applies to the dynamic scope; two tokens target one owned bot.
- Mounted bots follow 3 yards further out than their on-foot slot outside combat and instances.

### `.bot strategy`

Adds or removes strategies on a bot or the whole party.

```text
.bot strategy [+|-|~]<strategy name> [@tank|@dps|@heal|@melee|@ranged|Name]
.bot strategy reset
```

- Multiword names match as a whole (`.bot strategy +aoe fear @dps`).
- A full strategy name beats reading its last word as a bot name.
- Comma-separated changes work with no spaces (`.bot strategy +loot,-passive @dps`).
- No suffix: uses your selected bot, else controllable party bots.
- Bare `reset` restores defaults on that scope.
- A role suffix selects matching party bots even with a bot selected.
- A name selects that online owned bot.
- Unknown selectors or names are rejected.
- A role with no match reports `No party bot matches role <role>.`
- Arguments must begin with `+`, `-`, or `~`.
- `co`, `nc`, and `de` select combat, non-combat, and dead engines in whisper commands (e.g. `/w <Bot> co +focus`).

| Selector | Matches |
| :--- | :--- |
| `@tank` | Tanks |
| `@heal` | Healers |
| `@dps` | Everyone except tanks and healers |
| `@melee` | Melee-range attackers, tanks and healers included |
| `@ranged` | Ranged attackers, tanks and healers included |

- `+focus` / `-focus`: single-target burn; keeps traps and totems (`ss` excludes specific spells).
- `+offdps` / `-offdps`, `+offheal` / `-offheal`: off-spec kits per [off-spec rules](../../classes/overview.md#healer-off-dps-offdps).

### `.bot loot`

Shorthand toggle for corpse looting. See [`.bot action loot`](#bot-action-loot).

```text
.bot loot [on|off]
```

### `.bot repair`

Shorthand repair command. See [`.bot action repair`](#bot-action-repair).

```text
.bot repair
```

### `.bot sell`

Shorthand sell command. See [`.bot action sell`](#bot-action-sell).

```text
.bot sell
```

### `.bot rest` <a id="bot-rest"></a>

Shorthand rest command. See [`.bot action rest`](#bot-action-rest).

```text
.bot rest
```

- Aliased as `.bot drink` and `.bot eat`.

### `.bot downtime` <a id="bot-downtime"></a>

Runs town errands on one of your own alt bots: trainer, junk sale, and repair.

```text
.bot downtime [botName]
```

- The alt walks to the nearest reachable trainer and learns every rank it can afford.
- It sells gray junk at a vendor and repairs its gear, then walks back and follows you.
- No name: uses your selected alt bot, or all your own party alts.
- Hired companions and pool bots are refused.
- Nothing reachable (or nothing needed): the alt says so and nothing changes.
- Bounded: never touches the auction house or mail.
- Never sells soulbound or equipped items.
- Spends its own gold only on trainer ranks, repairs, and flight paths.
- Gives up and comes back if the errands cannot be finished.
- Order only: nothing runs by itself.
- A follow or stay order while the alt is out cancels the errands at once.
- Alt bots already playing on their own refuse.

> [!NOTE]
> **Pet Stance, Autocast, and Boost Notes:**
> - There are no direct `.bot pet aggressive` or `.bot pet autocast` chat commands. Pet commands are issued via whisper (`/w <Bot> pet aggressive`, `/w <Bot> pet autocast <spell>`) or the proxy (`.bot command <Bot> pet aggressive`, `.bot command <Bot> pet autocast <spell>`).
> - There is no direct `.bot boost` chat command. Configure boost targets via `/w <Bot> boost target +<Name>` or `.bot command <Bot> boost target +<Name>`.
