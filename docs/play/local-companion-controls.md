---
id: local-companion-controls
title: Local Companion Controls
category: play
summary: Additional companion commands, role selectors, gear controls and raid assignments.
tags: [local, companions, raids]
relates_to: [play-commands, local-raid-tactics, local-companion-controls, local-configuration]
---

# Local Companion Controls

These retained local controls supplement the current upstream guides. They do not enable a tactic unless its documented opt-in or private default says so. No new live validation is claimed by this rebase.

Successful pull and pullback orders warn when companions remain on stay and may not be able to reach the fight. Use `.bot action follow` to release those stay orders. The temporary DPS wait for the pull does not produce this warning.

Puller order honors a selected bot first, then `patchwerk main`, then non-soaker tanks. Assigned `patchwerk soak` bots are fallback pullers only. Grouped warrior tanks emit `TankCast` diagnostic rows for successful combat casts.

### Collapsed Core Hounds in Molten Core

Bots skip collapsed Core Hounds (entry 11671), including a collapsed marked or
explicitly ordered target, and can select the remaining live enemies. A hound
that revives becomes eligible again. No extra strategy or command is needed.
This does not synchronize pack damage: the group must still finish the hounds
within their revival window. Ancient Core Hounds and Core Ragers are unchanged.

| Control | Syntax | Behavior |
|---|---|---|
| **Experimental BiS** | `.bot gearbis <Name>` / `.bot gearbis all` | GM-only provisioning of currently controlled level-60 hires, alive and out of combat/not casting/travelling. Matches actual unique dominant talent tree plus compatible role. Supports Protection/Fury Warrior, Arcane Mage, Holy Priest, Restoration Druid and Holy Paladin; skips unsupported bots. Native equip rejection restores equipment. See data/experimental-bis-report.html for exact locked item IDs and deployment status. Approved name mappings: Redemption Helm (22428) and Mantle of Faith (22515). |
| **Summon** | `.bot summon <Name>` / `.bot summon all` | Teleports one owned bot, or all currently bound ready companions, safely to your location out of combat. Works for your own bots and hired companions; no config switch needed. The summon takes about 3 s and puts the bot back on `follow` when it arrives (also if it was on `stay` or `guard` before the request: the summon is your later order). The latest explicit order wins: a stationary order you give after the summon request and before the bot arrives (a whispered `stay` or `guard`, `.bot stay <Name>`, `.bot guard <Name>`, or `.bot action stay\|hold\|comestay`) cancels that follow restore, so the bot arrives and keeps holding, anchored where it landed. A later `.bot action come` is the latest order again (come, then follow) and lets the follow restore run. |

| Control | Syntax | Behavior |
|---|---|---|
| `.bot strategy` | `<+/-/~strategy name> [@tank/@dps/@heal/@melee/@ranged/Name]` | Adds, removes or toggles a strategy, including names with spaces. With no suffix, affects the selected bot or all controllable party bots. A trailing role filter addresses matching party bots even with a bot selected; a trailing bot name addresses that bot. |
| Arlokk panther focus | `all +arlokk panther focus` / `all -arlokk panther focus` | Off by default; non-tank DPS prefers eligible engaged Zulian Prowlers, including during vanish. Orders and CC remain authoritative; see dungeon tactics. |
| Mar'li spawn focus | `all +marli spawn focus` / `all -marli spawn focus` | Off by default; DPS prefers eligible growing spawns while Mar'li is engaged. Orders, CC and tanks retain priority; see dungeon tactics. |
| Thekal balancing | `all +thekal balance` / `all -thekal balance` | Off by default; DPS prefers the eligible first-phase priest with most remaining health. Orders, CC and tank targets retain priority. Timing/positioning remain manual; see dungeon tactics. |
| Jin'do curse protection | `all +jindo curse` / `all -jindo curse` | Opt-in on the curing bot: preserve friendly Delusions in ZG so the player can damage shades. Hex remains eligible for magic dispel. See the dungeon tactics guide for limits. |
| Hakkar poison protection | `all +hakkar poison` / `all -hakkar poison` | Opt-in automatic cure guard on the curing bot: in ZG, preserve friendly Poisonous Blood, including the poison effect of Cleanse/Purify. Existing periodic cures/totems and custom casts remain manual; see the dungeon tactics guide. |

### Raid strategy macros

Use a whole strategy name. Put one optional role filter or bot name last:

```text
.bot strategy +geddon inferno
.bot strategy +patchwerk melee safety @dps
.bot strategy +patchwerk soak @tank
.bot strategy -geddon inferno @heal
.bot strategy +patchwerk main MainTankName
```

Replace `MainTankName` with the actual bot name. With no suffix, select the
boss or clear your selection to address the party; selecting a bot limits the
command to that bot. Role filters always address the controllable party.

Role definitions match chat filters and honor group role assignments:
`@tank` selects tanks; `@heal` selects healers; `@dps` excludes both.
`@melee` selects non-ranged bots, including melee tanks.
`@ranged` selects ranged bots, including ranged healers.
Patchwerk main and soak assignments still need separate named commands;
`@tank` cannot distinguish the main tank from soakers.

Use `-` to remove or `~` to toggle. Existing one-word strategies, named-bot
commands and comma-separated changes (such as `+loot,-passive`) still work.
Changes apply to all AI states and are saved through the existing strategy
persistence path. Ownership/GM and online-bot checks are unchanged. Unknown
strategies, unknown filters and unavailable named bots are refused. A complete
registered strategy name takes priority if its final word also names a bot.
The reply gives the number of bots addressed; no role match reports zero.

### Experimental locked hiring

Walter's explicit spec choice and `.bot hire warrior fury`, `.bot hire priest holy`,
`.bot hire mage arcane` now preserve the chosen specialization through provisioning.
Explicit requests do not fall back to another spec. Role-only requests may still pick
among compatible builds. Final feedback verifies the dominant talent tree and role.
New level-60 hires automatically receive the matching one of the six locked profiles;
unsupported or conflicting specs and ineligible states report a reason without gearing.
Use `.bot inspect` with a controlled companion selected to read actual spec, talent
points, effective role and equipped locked item count (17/17 means every slot matches).
Existing companions are not automatically regeared. The locked item choices are unchanged.

For isolated integration validation only, the main server config can set
`TortoiseBots.ExperimentalHireSelfTest = 1` (default 0, startup only). It creates new
ledger-backed disposable characters, hires a Fury warrior through the production
service, verifies the same inspection output, and dismisses its fixtures. It never
selects existing characters. Disable after testing. This headless test does not
exercise the human client's gossip UI or group acceptance.

### Experimental pull diagnostics

Rejected puller selection reports separate counts for dead, already-in-combat, other-map/instance, unavailable and non-tank companions. God mode does not bypass these existing eligibility rules. Native pull request failures now retain the puller feedback instead of silencing it. This changes diagnostics, not pull eligibility or execution.

### Experimental hire arrival

New hires are delivered to the hiring player before normal AI starts. Group membership alone no longer completes arrival; map/instance and proximity are checked. Existing companions are not moved by this provisioning change.

When the hiring player is already inside a raid map, initial hire arrival converts the shared party to a raid before teleporting. This uses normal core entry checks; it does not grant GM status or bypass instance restrictions. Arrival retry logging is bounded.

Initial hire diagnostics log HIRE_ARRIVAL on attempts 1 and 31, separating invite failures, pending transfers, and core teleport rejection, with map/instance, group and encounter state. Encounter state is context, not proof of a particular rejection cause.

Hire grouping and arrival checks use whole-raid membership, not five-player subgroup membership, so hires in raid groups 2-8 can complete arrival.

Pull target validation reports the first failed predicate in the puller whisper and a PULL_REJECT server log entry. The existing validation order and eligibility rules are unchanged; reason outputs are optional for other callers.

Explicit `.bot action pull` / `pullback` retries clear only the selected target and its creature-entry unreachable cooldown on the chosen puller and reset reach progress. Normal target validation and pathing remain active; automatic pulls keep avoidance. Controlled bots inside raid maps skip autonomous travel request/selection to avoid measured world-thread stalls.

Experimental TBPULL tracing records explicit command pull lifecycle and engine eligibility/prerequisite/execution results. Each command logs at most 48 distinct stage/action/result combinations; repeated identical decisions are suppressed. Automatic pulls are not traced. This adds no gameplay changes.

An active pull now requeues its pull action through `pull continue` after start until completion, invalid/dead target, puller death or the existing 15-second timeout. The one-shot continuation uses movement priority. This preserves normal reach and cast eligibility checks and does not extend the timeout. Runtime confirmation is required.

Bounded explicit-pull tracing also identifies reach wait/no-headway branches and chase movement dispatch (MoveTo, spline, jump or MoveChase). A successful action return alone does not prove actual movement.

Explicit ranged pulls inside dungeons use the module point-movement path to a shooting position on the puller side of the target. This avoids the core chase endpoint melee-reach requirement. Normal movement/path and hazard handling remain enabled; body pulls and automatic pulls retain their existing chase path. Gameplay validation is pending.

Experimental pull-start timing correction (2026-10-02): before reporting a ranged pull unreachable, the command permits at most three additional AI decisions, stopping as soon as movement starts or the target is in range. Each check reacquires the pull target. This allows the preparation action to hand off to movement; the bounded retry does not guarantee all path failures are resolved. Gameplay validation pending.

Temporary experimental `TBHOLD` event diagnostics record commanded DPS hold creation, engagement/return clock restamps, release requests and actual marker removal, including the original tank GUID/alive state. They do not change hold timing or combat behavior; no per-tick logging is added.

## Explicit Protection boss-pull opening

A plain `.bot action pull` against a creature flagged as a world boss prepares
its Protection Warrior puller in Defensive Stance before approach, and attempts
Shield Wall at the firing position before the shot. A missing or cooling-down
Wall does not prohibit pulling. A known, ready Wall that cannot be cast waits
within the existing pull timeout. Last Stand remains a critical-health emergency;
an already active Last Stand prevents the proactive Wall attempt.

For ten seconds from the shot (or body-pull engagement), that puller avoids
offensive stance changes, charge, intercept and overpower. Shield Block priority is
not changed. Preparation never casts during the bow windup, and once it has acted
the held party's join window is restarted just before the shot. Rage, shield
equipment, cooldowns and spell checks are not bypassed. Automatic pulls, trash and
pullback behavior are unchanged. Other tanks still assist normally; this is not
simultaneous multi-tank engagement or healer preparation.

### Optional Onyxia main-tank position

Choose ONE main tank, not the whelp tanks. Replace `TankName` with its name:

```text
.bot role TankName tank
.bot strategy +onyxia landing tank TankName
```

During both ground phases, this assigns Onyxia and holds the tank at the
northern ground point, away from the two whelp caves. It checks the full route
inside the central room and refuses unsafe or incomplete paths. Ordinary
chase, charge and automatic tank-facing movement cannot replace that route.
Threat abilities and emergency escapes remain available. The air phase is
unchanged. Start inside the cleared main chamber; this does not initiate a pull.

Player stay/follow/pull holds and attack orders or marks on another enemy take
precedence. An attack order on Onyxia still permits the fixed position. Keep
the raid on her flanks; this does not position the raid or create tank threat.
A tank already in a cave needs manual recovery. A blocked route reports
`OnyxiaTankPositionBlocked`; no unchecked chase fallback is used.

Disable with `.bot strategy -onyxia landing tank TankName`.
This is opt-in and does not persist a new position or change saved stay points.

### Optional Geddon Inferno tactic

`.bot command <BotName> all +geddon inferno` enables Inferno retreat and its
movement guard in both combat and reaction engines; `all -geddon inferno`
disables it. It is off by default and only acts against an engaged Geddon in
Molten Core. All roles, including the current tank, retreat. Existing
stay/follow/wait-for-attack/grind and pull-hold orders win; use a normal attack
order after the hold ends to allow encounter movement. Inspect `co ?` and
`react ?` to check the strategy; it is not listed by `raid status`.

Raid bomb movement remains encounter-scoped (Molten Core, Blackwing Lair,
Naxxramas, and Solnius when enabled). A carrier uses one bounded search area per
aura application and avoids ordinary movement back into its group. Explicit
stay/combat-follow/wait/grind/pull-hold orders take precedence. See the bounded
raid bomb runout section in [Dungeon Tactics](../play/local-raid-tactics.md).

Optional Golemagg DPS targeting: `.bot command <BotName> all +golemagg dps`
(skips Core Ragers during the active encounter); `all -golemagg dps` disables it.
Tank roles, explicit attack targets and configured attack raid marks override
the exclusion. See [Dungeon Tactics](../play/local-raid-tactics.md).

Optional Majordomo DPS targeting: `.bot command <BotName> all +majordomo dps`
(skips the active hostile boss in favor of available adds); disable with
`all -majordomo dps`. Tank/explicit-target/attack-mark exceptions still apply.

Optional Garr tank assignments: set tank role, then use
`.bot command <BossTank> all +garr boss tank` for the boss tank and
`.bot command <AddTank> all +garr add tank` for each add tank.
**First send `.bot command <AddTank> rti none` to each add tank:** bots default
to skull, which otherwise takes precedence when skull is on Garr.
Explicit attack/focus and hold orders still win. Tanks split engaged Firesworn
and prioritize assigned adds attacking non-tanks. Positioning and threat success
remain manual/live concerns. Disable with `all -garr boss tank` or
`all -garr add tank`; restore mark assist with `rti skull` if wanted.
See [Dungeon Tactics](../play/local-raid-tactics.md#optional-garr-tank-assignments).

Optional Garr DPS discipline: `.bot command <BotName> all +garr dps`; disable
with `all -garr dps`. Unassigned Firesworn and autonomous splash damage are
suppressed during Garr. Explicit attack/raid-mark assignments allow single-target
kills; tanks, healing and CC remain available. See the existing-effect and pet
autocast limitations in [Dungeon Tactics](../play/local-raid-tactics.md).

Onyxia airborne spread for ranged/healer hires is a separate explicit opt-in:
`.bot command <BotName> all +onyxia air spread`; disable with
`all -onyxia air spread`. Start in the central floor region described in
[Dungeon Tactics](../play/local-raid-tactics.md). Hold orders override it; ordinary movement
resumes on landing. This does not provide Deep Breath protection.

Onyxia add support: `.bot command <BotName> all +onyxia whelps` on selected
melee DPS/add tanks; disable with `all -onyxia whelps`. During flight their
normal targeting prefers engaged whelps. Existing explicit targets and attack
marks override the mode; leave the boss tank's role/orders under manual control.

Golemagg tank assignment requires existing tank roles. Use
`.bot command <BotName> all +golemagg boss tank` on the boss tank and
`all +golemagg add tank` on the add tank; disable each with `all -...`.
Explicit targets/attack marks and hold commands win. One add tank holds both
Ragers; additional opted-in add tanks split them. See [Dungeon Tactics](../play/local-raid-tactics.md)
for live fallback and manual-control limits.

Ebonroc handover: `.bot command <BotName> all +ebonroc swap` on participating
tank-role bots; disable with `all -ebonroc swap`. Position replacements within
taunt range first. The tactic selects one ready unmarked tank and uses bounded
attempts; explicit hold/other-target orders win. See [Dungeon Tactics](../play/local-raid-tactics.md)
for capability, confirmation and retry limits.

### Role consumable following

With role consumables enabled by the server, grouped companions follow your
supported flask, elixir, scroll and food buffs using role-appropriate items from
their own bags. Your rewarded quests unlock categories; ask your server operator
which quests apply. Health/mana potions respond to low resources in combat.
Use `nc -role consumables` and `co -role consumables` to stop this automation, or
`nc +role consumables` and `co +role consumables` to restore it. Item-use orders
remain separate from this automation. See the role-consumables section of the
[configuration guide](../server/local-configuration.md) for the default-off hired-bot
provisioning option and catalogue limits.
Razorgore phase-one discipline: `.bot command <BotName> all +razorgore phase1`
on raid bots before the orb/add phase; disable with `all -razorgore phase1` if
the encounter resets. It blocks autonomous damage to Razorgore until the core
disables the Orb of Domination after the eggs are destroyed. Orb operation,
add pickup and egg destruction remain manual. Explicit attack/raid-mark orders
still win.

Vaelastrasz tank handover: `.bot command <TankBot> all +vael tank handover` on
every participating tank bot; disable with `all -vael tank handover`. Keep
replacement tanks in taunt range. Burning Adrenaline runout remains handled by
the bounded raid bomb tactic; if no replacement can taunt, recover the boss
manually. See [Dungeon Tactics](../play/local-raid-tactics.md) for selection and retry
limits.

## Maexxna web-wrap rescue (opt-in)

Enable `.bot command <BotName> all +maexxna rescue` on selected damage bots;
disable with `all -maexxna rescue`. Tanks and healers are excluded even when
enabled. Existing attack orders, attack raid marks and hold orders win.
During Maexxna in Naxxramas, prefer a live wrap with a live grouped prisoner
carrying both Turtle wrap auras. Ranged bots need current spell-range proximity
and line of sight; melee bots only select a wrap already in melee reach.
No automatic wall-climbing or path to elevated prisoners is attempted by this
selector. Position rescuers manually before the pull. The normal damage kit
still needs usable attacks; this does not grant spells or guarantee a release.
Current accessible wraps remain preferred until freed/dead; then normal DPS
selection resumes. Passive wraps use the cached nearby-target list rather than
requiring threat. No shared attacker filter or tank target is changed.


## Anub'Rekhan Locust Swarm retreat (opt-in)

Enable `.bot command <BotName> all +locust swarm retreat` on non-tanks;
disable with `all -locust swarm retreat`. Tanks are never automatically kited.
In Naxxramas, while engaged Anub'Rekhan15956 carries28785, non-tanks retreat
to at least33yd combat distance (30yd spell radius plus margin). Normal chase,
formation and charge movement are suppressed until the aura ends. Stationary
healing remains available; bomb escape and known-hazard movement can still win.
Stay/follow/wait/grind/pull holds override the tactic. Destination checks include
same floor, LOS, a complete path, known hazards and nearby unpulled enemies.
Paths turning closer to the boss are rejected at their vertices. The boss can
move during travel; the next action checks its new position. No sampled-route
or moving-boss safety guarantee is claimed. Failed paths require intervention.
Keep the tank route under manual control. Pets are not automatically recalled.


## Onyxia directional Deep Breath escape (opt-in)

Enable `.bot command <BotName> all +onyxia deep breath` on participating bots;
disable with `all -onyxia deep breath`. The existing server warning packet
is matched against Onyxia's localized name and loaded warning text; her current
launch endpoint identifies one of eight directions. Triggered breaths do not
occupy an ordinary current-cast slot. Missing/mismatched warnings do not trigger
a guessed retreat. No new core hook or database query is used.

The selector checks every loaded damage-circle position/radius in that breath
chain. It chooses a nearby reachable floor point outside all circles with a
3yd margin, inside the scripted launch perimeter, away from both egg-pit
entrances. Floor height, LOS, complete navmesh paths, known hazards, path samples
and nearby unpulled enemies are checked. Paths are at most50yd; at most24
spatially separated candidates receive path checks per action. Existing chase
stops even when no escape route passes. Hold orders override the feature.

For15sec after the warning, normal return/chase/charge and air spread yield to
the escape. Stationary casts and known-hazard escapes remain possible. Death,
landing or instance changes release the guard. Automation only operates inside
the checked room perimeter; outside it, move manually. Pets are not recalled.

High runtime risk until tested for all eight actual breath directions. This
checks geometry, not a guarantee of arriving before the five-second warning
ends. Slow/late/far-away bots can still be hit. Perimeter/egg margins come from
scripted coordinates, not a surveyed egg collision boundary. Watch every bot
and use manual movement when no route is accepted. No phase-three recovery
or full Onyxia survival claim is made.


## Ossirian designated crystal clicker (opt-in)

Enable `.bot command <BotName> all +ossirian crystal` on a chosen damage bot;
disable with `all -ossirian crystal`. Tanks/healers and explicit target/RTI or
hold orders are excluded. Position the bot within normal crystal interaction
range, then release movement holds. No automatic crystal search route or tank
movement is provided. The bot must be stationary, alive, in combat and able to
interact normally. Crystals must be visible, spawned, ready, usable and in LOS.

The human tank must stop Ossirian within25yd center distance of the crystal
and keep him there through the5sec weakness cast (actual radius30yd). The
clicker acts only with Strength25176, no weakness, or at most6sec weakness
remaining. Among opted-in ready group bots, the lowest GUID clicks; an
instance/boss claim spaces requests at least8sec and allows at most two
requests for one crystal. A queued request is not confirmation of weakness.

Watch for the actual weakness aura. Failed requests, player clicks, boss
movement during the cast, tornadoes and LOS can still waste a crystal. Exhausted
retries require manual intervention. This is assisted crystal use, not a full
Ossirian encounter controller.


## Grobbulus growing cloud evasion (opt-in)

Enable `.bot command <BotName> all +grobbulus clouds`; disable with
`all -grobbulus clouds`. Map533 cloud creatures16363 become live movement
hazards, including clouds from Injection and clouds remaining after boss death.
The radius follows aura28158's actual age:2 to20yd, with3sec growth lookahead
and2yd margin. Missing/invalid aura duration uses the conservative30yd spell
fallback plus2yd. A short-lived calculated value refreshes the circles; no
fixed-radius entry is left behind in stored hazards.

The escape checks every overlapping circle, not only the nearest center. It
tries bounded floor/path/LOS-safe destinations outside all known hazards,
rejects route samples that enter a new circle or move deeper into an initial
one, and avoids nearby unpulled enemies. Normal chase/charge yields while
inside; explicit stay/follow/wait/grind/pull holds win. Bomb runout and cloud
escape remain mutually permitted. Emergency cloud escape can exceed the bomb
runout budget, like other emergency hazard movement.

Start with damage/healer bots. Enabling on a tank permits movement that can
turn or move Grobbulus; tank routing remains your responsibility. Clouds can
grow during travel, geometry can block escape, and overlapping circles can
leave no accepted route. Pets are not recalled. Cure timing is unchanged and
belongs to the separate Injection interlock. No full-room/drop-slot plan is
provided. Actual cloud damage and Injection overlap need in-game validation.


## Kel'Thuzad Frostbolt interrupt ownership (opt-in)

Enable `.bot command <BotName> all +kt interrupt` on two or more interrupt-capable
bots; disable with `all -kt interrupt`. This applies only to engaged Kel'Thuzad
in Naxxramas. The lowest-GUID ready bot attempts the single-target Frostbolt
28478. A different ready bot may retry after 400ms if it is still being cast.
At most two attempts are made in four seconds; the pinned boss repeats that
cast no sooner than five seconds. Cast return values do not confirm interrupts.

Kick, Pummel, Shield Bash, Earth Shock and Counterspell are considered only
when known, usable now, in range/LOS, correctly facing and not immune. No stance
switch, movement or interruption of the bot's own current spell is forced.
Have several interrupt bots available; already casting bots may not be ready.
Explicit hold/other-target orders win. Normal spell actions for these five abilities
against this boss are reserved while the option is active, including
Earth Shock used for damage. Attacks on other targets are unaffected.

Pet Spell Lock and other abilities are outside this coordinator. Pet autocast,
humans and bots without the option can still spend interrupts independently. Frostbolt Volley and Frost
Blast are excluded. Reaction delay, missed casts and unavailable abilities can
still let Frostbolt land. This is not complete Kel'Thuzad encounter handling.

## Loatheb spore protection

`.bot command BotName all +loatheb spore guard` prevents autonomous spore
selection and many incidental damage actions during engaged Loatheb combat.
Use `-loatheb spore guard` to disable it. Direct attack orders and the configured
attack mark allow a chosen spore kill. Position spore groups and schedule heals
manually; ongoing area effects and autonomous pet splash still need oversight.

## Patchwerk tank assignments and melee entry (opt-in)

Assign exactly four bot tanks. Set roles before strategies:

```text
.bot role MainBot tank
.bot role SoakOne tank
.bot role SoakTwo tank
.bot role SoakThree tank
.bot command MainBot all +patchwerk main
.bot command SoakOne all +patchwerk soak
.bot command SoakTwo all +patchwerk soak
.bot command SoakThree all +patchwerk soak
.bot command DamageBot all +patchwerk melee safety
```

Replace names; repeat the last command for each melee DPS bot. Remove an option
with `all -patchwerk main`, `all -patchwerk soak` or `all -patchwerk melee safety`.
Do not give one bot both tank options. This first assignment slice requires a
bot main tank; a human main tank is not recognized by the automatic entry gate.

Assigned tanks prioritize engaged Patchwerk. Soakers suppress their ordinary
taunts against him, while keeping normal tank abilities and melee positioning.
On an explicit `.bot action pull` by the assigned main, soakers first prepare
their stances and gather beside that tank. The main waits before advancing or
firing until the assigned online soakers are alive, mobile, in the same instance,
nearby and ready. Soakers follow the main's current position, not idle Patchwerk,
and never attack during this staging. The existing 15-second pull timeout ends
an unsuccessful rendezvous. Canceling the pull also cancels staging.
Pullback and automatic pulls keep their existing behavior.

Once Patchwerk engages, the soakers use the combat opening below. Gathering
before the shot removes the long catch-up run from the raid's waiting spot;
it does not guarantee simultaneous arrival through blocked paths or crowd control.
No extra DPS hold is added. Keep all four tanks near one another before pulling.

An assigned soaker also joins when Patchwerk engages a raid member, including
on a normal pull or a human tank's pull. The bot first uses Defensive Stance
(or a known bear form), then attacks and moves into melee. Discovery takes
up to one second and requires the boss within sight distance in the same instance.
It does not wait for the soaker's own combat flag or the DPS pull timer.

For this encounter only, `patchwerk soak` overrides stay/follow and wait orders.
The strategies and saved stay point remain intact and resume after death/evade
or `all -patchwerk soak`. Remove the option to stop this automatic engagement.
This exception does not apply to the main tank, DPS, other bosses, or fresh pulls.
The soaker opening does not add a DPS hold or enable melee safety automatically.

Position the main tank and soakers close enough before pulling. Facing,
sustained health and threat generation still need supervision and healing.

Melee safety permits ordinary approach only when the assigned main is actually
tanking, and it plus three living, nonimmune, in-range soakers fill the first
four eligible threat positions. The bot tests its own hypothetical entry too:
an out-of-range DPS bot above a soaker on threat is not admitted. An unsafe bot
already near the boss stops attacking and tries a bounded outward retreat.
Hold and other-target orders win. No forced pull, teleport or tank promotion.

The actual core chooses highest current health among those four, excluding the
main tank; maximum health and tank role alone are insufficient. Health ordering
among assigned soakers is left to normal healing, not manipulated by this tactic.
A soaker death, immunity, range loss or DPS threat spike can invalidate the setup
before a bot escapes. The 1.2-second Hateful cadence can beat AI reaction/travel.
Blocked paths require human rescue. Pets and unopted players remain outside the
gate. This is a controlled benchmark aid, not guaranteed Hateful protection or
a Patchwerk kill; validate with the healing changes separately.

For Nefarian phase-three add assignment, enable `.bot command <TankBot> all +nefarian add tank` on participating tank bots; disable with `all -nefarian add tank`. This assigns only visible Bone Constructs (14605) and Corrupted Infernals (14668), preserves current tank victims and explicit/raid-mark orders, and leaves all other targets to normal selection. See [Dungeon Tactics](../play/local-raid-tactics.md).

### Thaddius placement assistance

`.bot command BotName all +thaddius polarity` enables optional, non-healer
placement on opposite charge anchors during engaged Thaddius combat.
Use `-thaddius polarity` to disable it. Coordinate charge swaps manually: this
option rejects occupied destinations rather than planning a group rotation.
Explicit orders take precedence; failed placement does not lock movement.

## Faerlina Worshipper guard

`.bot command BotName all +faerlina worshipper guard` preserves unassigned
hostile Worshippers during engaged Faerlina combat. Enable it on damage bots;
remove with `all -faerlina worshipper guard`. Direct attack orders and configured
attack marks override target protection. Control users must position their
Worshipper and cast Widow’s Embrace manually. Existing area effects and pet
splash remain manual responsibilities.

### Kel'Thuzad fissure escape

After installing this option, enable it for each raid bot:

```text
.bot command BotName all +kt fissure
```

Replace `BotName` with the character name. Use `-kt fissure` to disable it.
This is a best-effort escape from Shadow Fissure only. Manual hold strategies
prevent it; other Kel'Thuzad phase assignments remain manual.

### Onyxia Deep Breath diagnostics

For a repeat test of an enabled Deep Breath strategy, use the real bot name:

```text
.bot command BotName all +onyxia deep breath
.bot command BotName co ?
.bot command BotName nc +debug
```

The existing debug strategy also enables general AI debugging. During this
opt-in test, `bot_events.csv` receives `OnyxiaBreath` stages for warning
acceptance/rejection, inactive observations, movement availability, and route
results. Each stage is limited to once per bot per 15 seconds. Coordinates in
the detail field are raw map coordinates. No arbitrary chat text is recorded.
`move-accepted` means movement dispatch accepted the request, not that the bot
reached safety. This diagnostic change does not fix or guarantee breath evasion.
After the test, remove debug if it was enabled only for this test:

```text
.bot command BotName nc -debug
```

### C'Thun shelled-body targeting

Use `.bot command <BotName> all +cthun carapace` on a DPS bot to avoid
autonomously attacking the body during Carapace. Disable with
`all -cthun carapace`. Explicit orders, raid marks and tanks remain exempt.
See [the encounter limits](../play/local-raid-tactics.md#cthun-carapace-dps-guard-opt-in).

### Instance admission feedback

A controlled companion reports the core's hourly instance-limit rejection to its current human controller, naming the bot and the shared account limit. Repeated notices from that bot to the same controller are limited to one per30seconds. This also covers deferred admission failures and other entry attempts; it does not bypass any instance limit. Other rejection reasons are not described as the hourly cap.

Fankriss DPS targeting is opt-in with `all +fankriss spawn focus`; disable with
`all -fankriss spawn focus`. See [Dungeon tactics](../play/local-raid-tactics.md#fankriss-spawn-focus-opt-in).

Sartura retreat is opt-in with `all +sartura whirlwind`; disable with
`all -sartura whirlwind`. Manual combat hold strategies take precedence. See
[Dungeon tactics](../play/local-raid-tactics.md#sartura-whirlwind-retreat-opt-in).

Skeram image targeting is opt-in with `all +skeram image focus`; disable with
`all -skeram image focus`. See [Dungeon tactics](../play/local-raid-tactics.md#skeram-image-focus-opt-in).


### Kurinnaxx tank handoff

On each participating tank, use `.bot command <BotName> all +kurinnaxx swap`.
Disable with `all -kurinnaxx swap`. This is off by default and requires two
bot tanks already assigned to Kurinnaxx; explicit attack and hold orders win.
See [the encounter policy and limitations](../play/local-raid-tactics.md#kurinnaxx-mortal-wound-handoff-opt-in).

### Fixed Geddon bomb spot

Before pulling, park the raid. Stand at the desired bomb point on the left.
Keep it at least 22 yards from every other raid member, outside Inferno,
and within a short, clear run from melee. Then type:

```text
.bot action bombspot
.bot action bombspot status
```

This arms all your live owned bots in the same Molten Core instance,
including melee. Selecting a bot does not narrow this command.
Return to the raid before pulling. A carrier temporarily overrides parked
orders, runs to the marked point (or a safe point within 12 yards of it),
waits until Living Bomb clears, then returns
to where it started. It does not attack or cast during this sequence.
No extra strategy is required; setting the point is the opt-in.

```text
.bot action bombspot clear
```

Clear disables fixed mode; ordinary bomb behavior remains available.
The point is temporary: set it again after restart, bot relog, group change,
or instance change, and for newly joined bots. Set/change it out of combat.
A blocked return releases normal AI after 30 seconds.

Turtle Living Bomb explodes after 8 seconds, then leaves Molten Ground for
60 seconds. Carriers use nearby points outside live raid-owned ground fire
and at least 22 yards from the raid. If that small area is covered, they report
no safe route instead of running farther across the room. The command does not
move the raid or promise safety from roots, late detection or blocked routes. A rejected route produces a bomb-spot error and log event.


Hazard avoidance preserves accepted waypoint detours in the path sent to movement. If neither side is valid, it does not invent a replacement waypoint.

Talent links accept at most three trees (two `-` separators); malformed extra trees are rejected before parsing.

### Autonomous class-quest travel

When autonomous quest travel prioritizes class quests, it retains their destinations and excludes ordinary active quests from that priority pass. If no class quest has an eligible destination, ordinary quests remain available. This does not change explicit movement orders.

### Manual NPC dialogue diagnostic (GM only)

With an NPC selected, `debug monstertalk impersonate <text>` makes that NPC speak the supplied text; text starting with `*` is an emote. The manual conversation records the NPC as the speaker. An empty impersonation command does nothing. This moderator diagnostic does not change normal bot chat or command permissions.

The `patchwerk main` pull handoff arms melee and releases the main tank’s
threat rotation as soon as its pull shot registers threat. Rendezvoused
soakers also wake that handoff before their first attack if they update first.
This does not delay soakers or DPS, taunt, or change other boss pulls.

With a `patchwerk main` assigned, DPS also wait during that tank’s commanded
approach and for `AiPlayerbot.PatchwerkOpeningDelay` seconds after engagement
(default 4; configurable 3–5, or 0 to disable). Tanks and healers never wait.
The shared clock expires once per encounter; attack commands, late joins and
low tank threat do not extend it. This is separate from the older opt-in
`patchwerk melee safety` strategy and general `threat` behavior. Disable those
separately if the desired test is an opening delay with unrestricted later DPS.
Assigned Patchwerk warrior tanks use affordable Shield Slam / Revenge before
Sunder filler, including when the shared Sunder debuff already has five stacks.

### C'Thun spread (optional)

Enable this before entering C'Thun's room. With no bot selected:

```text
.bot strategy +cthun spread
```

For one macro that always addresses all three roles:

```text
.bot strategy +cthun spread @tank
.bot strategy +cthun spread @dps
.bot strategy +cthun spread @heal
```

Use the same commands with `-cthun spread` to turn it off.
A trailing bot name also works. The command saves the strategy selection.

Arrange eight raid groups of five, with healing coverage in each group.
Facing into the room from its entrance, groups 1/3/5/7 occupy the left
sectors and groups 2/4/6/8 the right, as in the supplied raid chart.
Each group has one red inner spot, two green middle spots and two yellow
outer spots. Tanks/melee receive the inner spots first; ranged and healers
receive the remaining middle/outer spots. GUID breaks ties within a role.
Death and swallowing retain roster reservations. Human players and bots
without the strategy must take their group's unused spots manually.

Only the surface room and its western approach/ramp on map 531 are affected.
Before a pull, entry spreading still requires being inside the room boundary
and in LOS. Once the Eye is engaged, enabled bots on the approach enter even
if they are not in combat or cannot see the Eye. An accepted ordinary
`.bot action pull` targeting the Eye also starts this entry movement before
the shot; a raid mark or merely selecting the boss does not. Pullback is not
an entry signal. Entry movement bypasses the pull's DPS wait without
releasing attacks early.

Recommended preparation point: **map 531, (-8675, 1968, 108.90)**.
The extracted ground navmesh and static LOS were checked at the point and
at 29 samples within a three-yard footprint. Prepare there while the Eye
is idle; keep the group compact within that footprint and keep pets back.
For a GM test, the coordinate command is:

```text
.go xyz -8675 1968 108.90 531
```

This moves the player, not the raid. Arrange/summon the raid only while
safely out of combat, then check everyone is at the preparation point.
Do not revive corpses inside the room while preparing a repeat.
The entry path uses the real ramp mouth at (-8640, 1975), then the existing
fixed sector lanes. The chart's slots and group order are unchanged.

**Out of LOS is not protection after the pull.** Turtle's Eye Beam 26134
ignores LOS for both casting and chaining. The preparation point prevents
a premature proximity pull; the raid must leave promptly once combat
starts. The old point (-8679.15, 1975.54, 108.90) is also blocked by the
extracted static geometry, despite the beam hitting the raid there.

Valid chart movement takes precedence over follow/stay and ordinary reach
movement; removing the strategy restores those orders. Fixed endpoints are
over 10 yards apart and clear of the eight small-eye birth points. Complete
navmesh paths and hazard checks still apply, including the approach leg.
Spacing is a settled-layout guarantee, not a promise during entry,
knockbacks or when humans occupy another bot's spot. Long routes to the far
sectors take time; the strategy does not guarantee a safe first beam or
assign an isolated first runner. The strategy cannot move humans or a
second raid out of its slots.

At a spot, DPS select targets they can hit with range and LOS: local small
eyes first, then giant eyes, local claws, then the Eye or unshelled body.
Nearby sectors can receive ranged help. No damage fallback selects the
shelled body or downstairs Flesh Tentacles. Explicit attack orders and
raid marks still win. Normal class attacks, pet attacks and heals continue.
Extra melee wait when nothing is reachable; spacing takes priority over
chasing. Tanks retain ordinary pickup; a tank holding a Giant Claw is
exempt from chart movement to preserve contact. Keep healing coverage
within each subgroup. Healers are never assigned compulsory DPS.

This is **not a complete C'Thun strategy**. It does not dodge Dark Glare,
assign interrupts or Giant Claw tanks, or handle the stomach. Downstairs
players are excluded from surface movement. Keep those duties manual.
A genuinely incomplete or hazardous path still logs `CthunSpreadBlocked`.


## Fankriss boss tanks (opt-in)

Use two warrior or bear tanks for the boss, with an optional third spare.
Keep at least one other tank assigned to hatchlings. Replace the example names:

```text
.bot role Faelor tank
.bot role Caelia tank
.bot command Faelor all +fankriss tank
.bot command Caelia all +fankriss tank
.bot strategy +fankriss spawn focus @dps
```

Pull normally with a chosen boss tank. The role does not start a fresh pull.
Assigned tanks keep attacking Fankriss instead of picking loose hatchlings.
At five Mortal Wound stacks, one ready tank with **zero** stacks taunts.
The outgoing tank keeps building threat; after its wounds expire, it can take
another turn when the new tank reaches the threshold. A third tank is a spare,
not an extra taunt on a healthy tank. Warriors use Defensive Stance and Taunt;
druids use bear form and Growl. Paladins have no supported Classic swap taunt.
Ordinary automatic boss taunts from assigned tanks are suppressed; manual cast
orders still work. DPS are not held or threat-capped by this role.

Both boss tanks must be close enough to attack/taunt. Normal stay and movement
rules still apply: release stay before pulling if the tanks need to move.
An explicit attack or valid raid-target order on a different enemy wins over
this role. Remove those orders before expecting automatic boss duty.

Server setting `AiPlayerbot.FankrissSwapStacks = 5` changes the threshold (1–9,
clamped; restart to apply). Setting it does not enable the role. If no healthy,
in-range tank has a usable taunt, the swap waits; it cannot guarantee survival.
An assigned clean tank can also taunt if Fankriss is attacking a raid non-tank.
Failed/resisted attempts can retry after two seconds; actual spell cooldown,
stance, LOS, range and immunity checks still apply.

Disable on both tanks after the encounter:

```text
.bot command Faelor all -fankriss tank
.bot command Caelia all -fankriss tank
```
### Twin Emperors opening assignments

`all +twins veklor tank` assigns a warlock; `all +twins veknilash tank`
assigns a warrior. Add exactly one side: `all +twins east` or `all +twins west`.
Use `.bot role <BotName> tank` for healing recognition.
DPS chooses either `all +twins physical dps` or `all +twins spell dps`.
See the Twin Emperors section of the local raid tactics guide for staging,
full commands and the remaining teleport limitations.

## Huhuran threat tanks and ranged spacing (opt-in)

Huhuran cannot be handed over with Taunt. Assign two or three **already geared
and configured tanks**; all must build ordinary threat from the start. Replace
`TankOne`, `TankTwo` and `HealerOne` with character names:

```text
.bot role TankOne tank
.bot role TankTwo tank
.bot command TankOne all +huhuran tank
.bot command TankTwo all +huhuran tank
.bot command HealerOne all +huhuran ranged
```

Repeat the tank commands for an optional third tank, and the ranged command for
each ranged DPS/healer who is **not** one of the 15 designated nature-resistance
soakers. Do not enable ranged spacing on the fifteenth soaker just because that
player is normally ranged. Leave both strategies disabled unless assigned.

Pull with the chosen first tank normally. The role only finds an already-engaged
Huhuran attacking this group in AQ40; it never starts a fresh pull. Assigned tanks
join in combat or non-combat, select Huhuran over loose-target tank assistance,
prepare Defensive Stance/bear form when learned, and keep their normal threat
rotation. Their opening attack delay and ordinary damage threat back-off are
bypassed only while this assignment is active. Automatic taunt actions (including
Growl, Mocking Blow and area taunts) are vetoed in combat/reaction engines.
Do not issue manual taunt/cast orders: this is not a spell prohibition in the core.

There is no Acid Spit stack election, forced threat change or scripted taunt.
A death or threat drop lets the next actual threat holder take over naturally.
It may be a non-tank if the backup tanks have not built enough threat; gear,
healing, rage and normal hit/cast checks still matter. The role does not throttle
DPS or guarantee a particular tank order. Tanks keep attacking rather than
stepping out to shed stacks.

`huhuran ranged` moves a ranged DPS/healer outward when too close to the engaged
boss. It aims for 24 yards from the boss center, or three yards beyond the
fifteenth closest living, same-instance raid member **without this ranged role**,
capped at 28 yards. Protected ranged bots do not push each other farther out.
A one-yard tolerance avoids constant repositioning. With fewer than 15 eligible
soakers it keeps the 24-yard target, but cannot keep everyone out of the volley.
These are approximate center distances, not a guarantee of spell target ranking.

Spacing finishes an existing cast before moving, checks height/LOS/path/hazards,
and retains ordinary movement safety checks. It does not kite Huhuran if the
ranged bot becomes her victim. Healing reach remains available. Short offensive
spells cannot repeatedly pull a settled bot into melee; ordinary out-of-range
recovery still runs. Bad terrain, cast pressure, healing reach, insufficient
soakers or scattered players can prevent ideal spacing. NR gear, buffs, potions,
Tranq assignments and careful Wyvern Sting handling remain the raid leader's job.

Explicit stay/guard/combat-follow and valid alternate attack/raid-mark orders win.
Release parked orders and clear conflicting targets before pulling; adding a role
does not cancel them. Existing non-combat follow is retained. Ranged spacing also
stands down during `wait for attack`; assigned tanks alone bypass that opening
wait. Disable the assignments after the encounter:

```text
.bot command TankOne all -huhuran tank
.bot command TankTwo all -huhuran tank
.bot command HealerOne all -huhuran ranged
```
