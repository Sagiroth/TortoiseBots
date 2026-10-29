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

TortoiseBots uses an intent-driven control model for combat and movement. Instead of micromanaging individual abilities, you issue high-level tactical orders; the server evaluates party roles and dispatches actions to the most capable bots.

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
| [`.bot action stop`](#bot-action-stop) | Controller | None | Clears combat queues and stops current attacks. |
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
| [`.bot action free`](#bot-action-free) | Controller | None | Releases bots to autonomous movement. |
| [`.bot action raid status`](#bot-action-raid-status) | Controller | None | Displays active raid strategies for party bots. |
| [`.bot action raid tankface`](#bot-action-raid-tankface) | Controller | Dragon Target | Directs tank to face dragon boss away from group. |
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

Orders scoped tanks and DPS bots (selected bot if targeted, else party) to engage your current hostile target.

```text
.bot action attack
```

Bots accept targets out to `AiPlayerbot.ReactDistance` (150 yards by default, follows config) and path directly toward them. Dedicated healers continue healing allies rather than dealing damage.

<details>
<summary>How it works in detail</summary>

- **Evade Handling:** Creatures in evade mode cause bots to whisper `"<name> is evading"`.
- **Unreachable Targets:** The command lifts any temporary unreachable flags on that mob family. If the target remains physically unreachable after 15 seconds, bots drop the target and ignore that mob family for five minutes.
- **Target Validation:** Dead targets are rejected with `dead-target`. Missing hostile targets are refused with `no-target`.
- **Stay Release:** Issuing `.bot action attack` ends any active stay or flee hold on all dispatched bots, so they can move to the target.

</details>

### `.bot action interrupt`

Directs the first available party bot with a ready interrupt ability to interrupt your target's spell cast.

```text
.bot action interrupt
```

The system checks registered interrupt abilities (including *Counterspell*, *Silence*, *Spell Lock*, *Kick*, *Pummel*, *Shield Bash*, *Earth Shock*, and related stuns). If the chosen bot is out of range, the mature reach action moves it into range before executing the interrupt. Refusal codes: `no-target`, `dead-target`, `not-casting`, and `no-interrupt`.

### `.bot action stop`

Clears active combat queues and stops ongoing attacks across all companion bots.

```text
.bot action stop
```

Bots stop attacking and moving, but keep normal follow movement.

### `.bot action flee`

Orders companion bots to disengage from combat immediately and follow the leader passively without attacking.

```text
.bot action flee
```

Bots drop their combat targets and pull holds, following you closely. The next tactical order that sends bots at a target or changes movement ends the passive hold.

### `.bot action pull`

Directs the party tank to pull your hostile target while other bots hold back at your position.

```text
.bot action pull [seconds] [ranged]
```

Parameters `[seconds]` (0 to 60, default 10) and `[ranged]` can be passed in either order. Other bots hold for the specified delay after the tank engages before joining combat. The tank runs its normal rotation without needing a second order. The join window always ends on its own clock; combat end, death, or a movement order only release the anchor hold.

<details>
<summary>How it works in detail</summary>

- **Melee and Ranged Pulling:** The tank walks into melee range unless it has an equipped ranged weapon and ammo. If the target is unreachable on foot or farther than the approach distance configured in [AiPlayerbot.RangedPullMeleeApproach](../../server/settings/combat.md#aiplayerbotrangedpullmeleeapproach) (default 20 yards), the tank pulls from range.
- **Forced Ranged:** Adding `ranged` requests a ranged opening — the tank walks to a firing position. If the tank cannot shoot, the blockers below apply and it body-pulls instead. Blockers are `no-ranged-weapon`, `weapon-untrained`, `shoot-spell-missing`, `no-ammo`, and `too-close` (whispered only if ordered ranged and blocker is not `NoWeapon` or `TooClose`).
- **Early Release:** The pull hold ends early if you issue `.bot action attack` or `.bot action flee` (issuing `stop` does not release the DPS join window).
- **Refusal Codes:** Refused if target is dead (`dead-target`), friendly (`friendly`), beyond `ReactDistance * 3` (450 yards at default settings, returning `too-far`), unable to start walking to a firing position (`unreachable`), or invalid (`invalid-target`). If no tank is available, returns `no-tank`, or `tank-busy` if the only tanks found are already fighting.

</details>

### `.bot action pullback`

Orders the tank to pull the target from range and immediately retreat to your current position, breaking line of sight around corners.

```text
.bot action pullback [seconds] [ranged]
```

Accepts `[seconds]` (0 to 60, default 5) and `[ranged]` in either order. The tank approaches only close enough to fire, shoots, and returns to your position. Other bots attack `seconds` after the tank is back at your position.

<details>
<summary>How it works in detail</summary>

- **Corner Pulling:** Use this maneuver to pull enemy packs around corners or through doorways to break enemy caster line of sight.
- **Anchor Hold:** The tank holds ground at the pullback spot only while the pulled enemy is still on its way. Once it arrives, stops short or dies, the tank fights the rest of the pack freely and follows you again after combat.
- **Fallback Melee:** If the tank has no working ranged weapon, it walks in, strikes the target once in melee, and sprints back to your position.
- **Meal Interrupt:** The order first interrupts a cast of the tank's (a meal) and stands it up, since the reach does not move a casting bot; a walk still not started after the order's decisions gets one direct reach.

</details>

### `.bot action come`

Commands scoped bots (selected bot if targeted, else party) to walk directly to your current coordinates and hold position.

```text
.bot action come
```

Aliases include `.bot action hold` and `.bot action comestay`. While `come` records a come order (allowing pending summon follow-restore to run), `hold` and `comestay` record a hold order. Bots arrive at your exact spot and switch their movement state to `stay`.

### `.bot action stay`

Halts scoped bots (selected bot if targeted, else party) at their current positions and makes them hold ground.

```text
.bot action stay
```

Bots stop moving immediately. Targeted combat orders automatically release dispatched bots from stay so they engage and follow once combat finishes.

### `.bot action follow`

Breaks current movements and orders scoped bots (selected bot if targeted, else party) to resume follow formation behind the player.

```text
.bot action follow
```

If [`AiPlayerbot.DungeonFollowHoldBeforePacks`](../../server/settings/party.md#aiplayerbotdungeonfollowholdbeforepacks) is enabled, bots in a dungeon or raid trailing while nobody is in combat stop outside unpulled packs and whisper you a warning instead of pulling accidental adds (resuming when the pack dies or the leader returns within 25 yards).

### `.bot action focus skull`

Applies the **Skull** raid target icon to your current hostile target (or targets the already Skull-marked mob if you have no target) and commands all party bots in scope to attack it.

```text
.bot action focus skull
```

Orders all party bots in scope to attack the Skull mark using their normal rotations (healers are not exempted here).

### `.bot action cc`

Assigns a crowd control (CC) raid target icon to a capable companion bot.

```text
.bot action cc <mark> [bot]
```

Supported marks include `moon`, `star`, `circle`, `diamond`, `triangle`, `square`, `cross`, and `skull` (`skull` is normally reserved for focus fire).

<details>
<summary>How it works in detail</summary>

- **Role and Spell Matching:** The server probes the mature crowd-control actions each bot knows (such as *Polymorph*, *Sap*, *Banish*, *Fear*, *Shackle Undead*, *Hibernate*, *Entangling Roots*, *Freezing Trap*, *Scare Beast*, and *Turn Undead*) and picks the best fit for the mark and target.
- **Exclusive Ownership:** Only one bot owns a specific mark at a time. Assigning a mark to another bot clears it from the previous owner.
- **AoE Protection:** Party bots avoid using area-of-effect spells near crowd-controlled targets to avoid breaking the control.
- **Pre-Pull Sap:** A rogue assigned a mark on a suitable target stealths and saps it out of combat before the pull by default.

</details>

### `.bot action cc clear`

Clears crowd control mark assignments from party bots.

```text
.bot action cc clear [bot]
```

Specifying a bot name clears only that bot's assigned mark; targeting an owned bot clears that bot; omitting both clears marks from all companion bots in the party. The accepted `.bot cc <mark> [bot]` / `.bot cc clear [bot]` forms behave identically.

### `.bot action auto cc`

Toggles smart autonomous crowd control on loose hostile adds.

```text
.bot action auto cc [on|off|status]
```

With no argument, it toggles the setting. When enabled:
- Activates in group combat against 2 or more enemies.
- Target must be a hostile hitting a party healer or ranged bot, never the tank, and nobody in the group may be attacking or targeting it.
- Target must have no periodic-damage aura, must not be the last mob alive, must not be Skull-marked, and must not be the tank's victim.
- One bot per mob, no re-sheeping active CC, and explicit CC marks always take precedence.

### `.bot action sap`

Controls whether rogues sneak up and apply *Sap* to their crowd control mark before a pull.

```text
.bot action sap [on|off] [bot]
```

Pre-pull Sap is on by default. Specifying a bot name (or targeting a bot) scopes to that bot, else all party rogues; non-rogues are skipped. If no rogues are in scope, returns `no-rogue`. The server acknowledges with `on`, `off`, or `mixed`. `sap=on|off` also rides the `TBM:BOTSTATE` line per bot.

### `.bot action aoe`

Toggles area-of-effect (AoE) abilities across all companion bots.

```text
.bot action aoe [on|off|status]
```

Bare `aoe` toggles the switch. AoE is on by default. Turning it off applies the `no aoe` strategy (shared with `.bot behavior <bot> aoe` and persisted across sessions), preventing bots from casting multi-target spells and abilities that might break CC. The `dps aoe` target picker also stops picking cluster targets while AoE is off.

<details>
<summary>Spells affected by AoE toggle</summary>

- **Mage:** Arcane Explosion, Blast Wave, Blizzard, Cone of Cold, Flamestrike, Frost Nova.
- **Warlock:** Rain of Fire, Howl of Terror.
- **Priest:** Holy Nova, Psychic Scream.
- **Druid:** Hurricane, Swipe, Swipe (bear), Demoralizing Roar, Challenging Roar.
- **Hunter:** Multi-Shot, Volley, Carve, Explosive Trap, Explosive Trap on target, Frost Trap, Frost Trap on target.
- **Paladin:** Consecration, Holy Wrath.
- **Shaman:** Chain Lightning, Magma Totem, Fire Nova, Earthquake.
- **Warrior:** Cleave, Whirlwind, Sweeping Strikes, Thunder Clap, Thunder Clap on snare target, Demoralizing Shout, Challenging Shout, Intimidating Shout, Piercing Howl.
- **Rogue:** Blade Flurry.
- **Other:** War Stomp, Goblin Sapper, Grenades (throw grenade), Oil of Immolation. The `dps aoe` target picker is also suppressed.

</details>

### `.bot action loot`

Toggles corpse looting on or off across companion bots.

```text
.bot action loot [on|off|status]
```

Corpse looting is on by default. Omitting the argument flips the current state and returns an acknowledgment with the resulting setting.

### `.bot action repair`

Orders companion bots to repair all damaged equipment at a nearby repair merchant.

```text
.bot action repair
```

Bots interact with the nearest NPC with the repair flag (`UNIT_NPC_FLAG_REPAIR`).

### `.bot action sell`

Orders companion bots to sell all gray-quality junk items in their inventory to a nearby merchant.

```text
.bot action sell
```

Bots find a nearby stocked vendor NPC themselves (`UNIT_NPC_FLAG_VENDOR`) and sell trash items to free up bag space.

If no bot sells anything, the reply says why: nothing grey to sell, no vendor in reach, or no grey item the bot will sell here.

### `.bot action rest`

Orders companion bots to sit, eat food, and drink water until their health and mana are fully restored.

```text
.bot action rest
```

Aliases include `.bot action drink` and `.bot action eat`. Mage bots can share conjured food and water with the party through their own give-food and give-water triggers. If bots cannot rest, the system responds with grouped reasons (`No bot rested: <reason>: <bots>...`, capped at 240 bytes with `+K more`):
- `dead`
- `in combat`
- `party still fighting`
- `nothing to rest (mana X% >= Y%, health A% >= B%)`
- `no food or drink` (or `no drink`, `no food`)
- `leader is moving`
- `other: not useful now`

### `.bot action release`

Commands dead companion bots to release their spirit.

```text
.bot action release
```

See [Roster and Hiring Commands: .bot release](roster-and-hiring.md#bot-release) for full details.

### `.bot action corpse run`

Commands companion bots in spirit form to path back to their corpse.

```text
.bot action corpse run
```

Alias: `.bot action corpserun`. See [Roster and Hiring Commands: .bot corpse run](roster-and-hiring.md#bot-corpse-run).

### `.bot action learn`

Commands companion bots to learn available spells and ranks from the selected trainer.

```text
.bot action learn
```

Target a trainer you have selected. See [Roster and Hiring Commands: .bot learn](roster-and-hiring.md#bot-learn).

### `.bot action trade`

Opens a direct player-to-bot trade window with your targeted companion bot.

```text
.bot action trade
```

See [Bot Panel: .bot item trade](bot-panel.md#bot-item-trade).

### `.bot action end pull`

Aborts a running pull maneuver across scoped bots.

```text
.bot action end pull
```

### `.bot action stance`

Sets per-bot combat facing across scoped bots.

```text
.bot action stance <near|tank|turnback|behind>
```

`turn` is accepted as `turnback`. The acknowledgement detail names the stance
each bot took.

### `.bot action rebuff`

Refreshes expiring party buffs out of combat across scoped bots.

```text
.bot action rebuff
```

Uses the same 2-minute expiration pass as the whisper form.

### `.bot action guard`

Orders scoped bots to hold ground at their current positions.

```text
.bot action guard
```

Captures each bot's current spot as its guard position.

### `.bot action free`

Releases scoped bots from `stay` or `guard` back to autonomous movement.

```text
.bot action free
```

### `.bot action ready`

Initiates a party-wide readiness check across all companion bots.

```text
.bot action ready
```

Bots evaluate health (above `AiPlayerbot.AlmostFullHealth`, 90 by default), mana for mana users (above `AiPlayerbot.MediumHealth`, 70 by default), distance (within `AiPlayerbot.SightDistance`, 60 yards by default), hunter ammo, pet presence and happiness, and food, water, and potion counts. Each bot reports its readiness status directly to the requester. Buff topping off occurs only on real raid checks when [`AiPlayerbot.ForceRebuffOnReadyCheck`](../../server/settings/world-population.md#aiplayerbotforcerebuffonreadycheck) is enabled (default 0, disabled), and only for bots with the `force rebuff` strategy.

### `.bot action raid status`

Displays the active raid encounter strategy currently configured for party bots.

```text
.bot action raid status
```

Reports active encounter strategies (including `molten core`, `onyxia`, `four horseman`, `emerald sanctum`, `lower karazhan`, `karazhan crypt`, and `solnius`) or falls back to `instance map <id>, no raid strategy` (or `outdoor`).

### `.bot action raid tankface`

Orders tank bots to position dragon bosses facing directly away from the raid group.

```text
.bot action raid tankface
```

Healers and damage dealers should stand clear of frontal cleaves and breath attacks.

### `.bot action raid douse`

Commands companion bots to douse nearby Molten Core boss runes. The bot first tries Eternal Quintessence, then falls back to Aqual Quintessence.

```text
.bot action raid douse
```

### `.bot action raid custom status`

Reports the active state of custom Turtle WoW raid encounter strategies.

```text
.bot action raid custom status
```

Outputs exact lowercase states: `emerald sanctum`, `lower karazhan`, `karazhan crypt`, or `off`.

### `.bot action raid custom on`

Enables custom raid encounter tactics for Turtle WoW raid instances.

```text
.bot action raid custom on
```

The manual command works regardless of configuration; [`AiPlayerbot.EnableCustomRaidTactics`](../../server/settings/dungeons-raids.md#aiplayerbotenablecustomraidtactics) gates automatic transition strategies on zone-in.

### `.bot action raid custom off`

Disables automated custom raid tactics, returning bots to standard manual control.

```text
.bot action raid custom off
```

---

## Direct Movement and Party Commands

Direct commands target one controlled bot by name (or the dynamic scope for toggles like loot and formation), while party-wide orders use `.bot action`.

### `.bot follow`

Orders an owned companion bot to follow you.

```text
.bot follow <Name>
```

A bot name is required (without a name, the server returns usage). For party-wide follow, use [`.bot action follow`](#bot-action-follow).

### `.bot stay`

Orders a specific companion bot to halt and stay at its current coordinates.

```text
.bot stay <Name>
```

A bot name is required. For party-wide stay, use [`.bot action stay`](#bot-action-stay).

### `.bot guard`

Orders a specific bot to hold ground at its current position and defend against nearby hostile enemies.

```text
.bot guard <Name>
```

### `.bot free`

Releases an owned bot from `stay` or `guard` back to autonomous movement.

```text
.bot free <Name>
```

### `.bot ready`

Runs a readiness check on an individual named bot.

```text
.bot ready <Name>
```

A bot name is required. Checks health, mana, distance, and supplies, reporting the result to the requester. For party-wide readiness, use [`.bot action ready`](#bot-action-ready).

### `.bot attack`

Orders a specific companion bot to attack your current hostile target.

```text
.bot attack <Name>
```

Ends any active stay or flee hold on the targeted bot; the bot engages the target and then resumes following you when combat ends.

### `.bot interrupt`

Orders a specific companion bot to interrupt your current target's spell cast.

```text
.bot interrupt <Name>
```

### `.bot pullback`

Dispatches a pullback maneuver on your designated tank (or selected bot).

```text
.bot pullback
```

Does not accept a name argument. Requires a live non-bot hostile target and a tank-capable bot (player-designated or native tank). The tank pulls from range and retreats to your position.

### `.bot formation`

Configures the follow formation used by companion bots while following the party leader.

```text
.bot formation [botName] <formation>
```

Supported formations:
- `chaos` (shipped default `AiPlayerbot.DefaultFormation`, resets with `default`)
- `melee`
- `queue`
- `circle`
- `line`
- `shield`
- `arrow`
- `near`
- `far`

Out of combat outside instances, mounted bots follow 3 yards further out than their on-foot slot (mounts are bigger than the 1.5-yard follow ring).

### `.bot strategy`

Adds or removes strategies on a bot or the whole party.

```text
.bot strategy [+|-|~]<strategy name> [@tank|@dps|@heal|@melee|@ranged|Name]
.bot strategy reset
```

Multiword strategy names are accepted as a whole, for example
`.bot strategy +aoe fear @dps` or `.bot strategy +tank assist TankName`.
A complete registered strategy name takes precedence over interpreting its last
word as a bot name. Comma-separated changes still work, for example
`.bot strategy +loot,-passive @dps` (no spaces around commas).

Without a suffix, the selected controllable bot is used, or otherwise the
controllable party bots. Bare `.bot strategy reset` restores default strategies on the same scope and replies `Strategy change 'reset' applied to N bot(s).` A role suffix explicitly selects matching controllable
party bots, even when a bot is selected. A name selects that online owned bot.
Ownership checks and strategy persistence are unchanged.

Role selectors use the same group-aware predicates as whisper role filters:
`@tank` and `@heal` match those roles, while `@dps` excludes both. `@melee`
and `@ranged` select attack-range categories, including tanks and healers.
Unknown selectors or strategy names are rejected; no matching role reports
zero bots changed. No new configuration or automatic behavior is enabled.

Arguments must begin with `+`, `-`, or `~`. (The prefixes `co`, `nc`, and `de` select combat, non-combat, and dead engines in whisper commands, e.g. `/w <Bot> co +focus`).

- **Single-Target Burn (`+focus` / `-focus`):** Suppresses multi-target attacks while keeping traps and totems intact (use `ss` / skip spell to exclude specific spells).
- **AoE Fear Toggle (`+aoe fear` / `-aoe fear`):** Suppresses Priest *Psychic Scream*, Warlock *Howl of Terror*, and Warrior *Intimidating Shout* inside dungeons and raids, and in the open world while grouped with you.

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

Aliased as `.bot drink` and `.bot eat`.

### `.bot downtime` <a id="bot-downtime"></a>

Orders one of your own alt bots to run its town errands by itself: walk to the
nearest reachable class trainer and learn every rank it can afford, sell its
gray junk at a vendor, and repair its gear. Afterwards it walks back and
resumes following you.

```text
.bot downtime [botName]
```

Without a name the order applies to your selected alt bot, or to all of your
own alt bots in the party. Hired companions and pool bots are refused.

The alt only walks when something is reachable: with no trainer or vendor in
range (for example out in the wild) it answers so and nothing changes, and an
alt that needs nothing answers that too. The errand is bounded: it never
touches the auction house or mail, never sells soulbound or equipped items,
spends its own gold only on trainer ranks, repairs and flight paths, and it
gives up and comes back if the errands cannot be finished.

> [!NOTE]
> **Downtime Notes:**
> - The command is on order only: nothing runs by itself, and a follow or stay
>   order while the alt is out cancels the errands immediately.
> - Alt bots that already play on their own (`.bot add` alts that left the
>   party) refuse: they already run these errands on their own.

> [!NOTE]
> **Pet Stance, Autocast, and Boost Notes:**
> - There are no direct `.bot pet aggressive` or `.bot pet autocast` chat commands. Pet commands are issued via whisper (`/w <Bot> pet aggressive`, `/w <Bot> pet autocast <spell>`) or the proxy (`.bot command <Bot> pet aggressive`, `.bot command <Bot> pet autocast <spell>`).
> - There is no direct `.bot boost` chat command. Configure boost targets via `/w <Bot> boost target +<Name>` or `.bot command <Bot> boost target +<Name>`.
