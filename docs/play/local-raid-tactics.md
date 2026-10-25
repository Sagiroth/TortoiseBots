---
id: local-raid-tactics
title: Local Raid Tactics
category: play
summary: Optional encounter controls, assignments and their current limitations.
tags: [local, companions, raids]
relates_to: [play-commands, local-raid-tactics, local-companion-controls, local-configuration]
---

# Local Raid Tactics

These retained local controls supplement the current upstream guides. They do not enable a tactic unless its documented opt-in or private default says so. No new live validation is claimed by this rebase.

### Thaddius: optional placement assistance

Enable `thaddius polarity` on selected non-healer bots with
`.bot command BotName all +thaddius polarity`. Disable with
`.bot command BotName all -thaddius polarity`. It is off by default.

Positive and negative charges use opposite anchors ten yards from the boss,
perpendicular to the Tesla-coil axis. The 2.5-yard arrival tolerance leaves
at least 15 yards between opposite arrivals, beyond the 13-yard damage radius.
Paths must clear current opposite-charge positions by 14 yards; a bot already
inside that margin may only move outward and must finish outside it. These
checks do not predict other players' movement.

This is placement assistance, not coordinated polarity-swap routing. If the
opposite group occupies the destination, the route is rejected. Normal movement
remains available whenever placement is needed or clearance is lost; the option
does not immobilize bots on a failed route. Ordinary movement is suppressed only
while the bot is safely settled at its assigned anchor. Handle charge swaps by
hand, disabling the option during the swap if needed. Explicit attack/RTI and
stay/follow/wait/grind/pull holds take precedence. Healers remain manual.


Heigan's core script skips the safe sections in the order 0, 1, 2, 3, 2, 1 during the 45-second dance, starting four seconds after the channeling yell and repeating every three seconds; the other three sections erupt. Playerbot does not observe that script phase or select a safe lane. The coordinates named `sect*SafeSpot` in core are where eruption visuals are sent in unsafe sections, not safe destinations. Keep the safety dance under player control until phase-aware movement is implemented and tested.

## Sapphiron Blizzard avoidance (opt-in)

Enable .bot command <BotName> all +sapphiron blizzard for bots that should step out of Blizzard; disable it with all -sapphiron blizzard. The tactic is active only in combat with Sapphiron and tracks every active spell 28547 damage area at its cast position, even after the Blizzard creature moves. It uses each area's actual loaded radius plus a two-yard margin and the existing generic hazard escape. Hold orders suppress the tactic. Ice Block cover is not automated: the core creates gameobjects, but the source does not establish that they block the playerbot line-of-sight check. Direct bots behind blocks manually during Frost Breath until tested in game.

Blizzard owner discovery is shared per bot and refreshed no more than once
per 500 milliseconds. Newly created areas can therefore take up to that refresh
interval to appear. Known area IDs are resolved live, so removed/expired objects
and current bot positions are checked without waiting for the discovery refresh.
Hold orders are checked before consulting the snapshot.

### AQ40 Viscidus glob focus (opt-in)

Enable `.bot command <BotName> all +viscidus glob focus` on non-tank DPS bots;
remove it with `all -viscidus glob focus`. During the exploded phase, opted-in
bots select a valid nearby Glob of Viscidus (`15667`), even though the glob is
not attacking the raid. Selection retains the glob only while it remains alive,
nearby, attackable and clear of crowd control or raid exclusions. Explicit
targets, raid marks and crowd control remain ahead of this behavior. This only
assists glob kills; it does not automate frost spell selection or the 200-hit
freeze phase.

### Twin Emperors: physical DPS assignment (opt-in)

Enable `.bot command <BotName> all +twins physical dps` on bots assigned to
physical DPS; remove with `all -twins physical dps`. In AQ40 combat, autonomous
DPS selection skips engaged Vek'lor (`15276`), who is immune to normal damage.
The bot can select another normally eligible target, including Vek'nilash;
if none is available it does not force a new target. Existing CC reservations,
attackability/range checks and explicit attack/raid-mark orders still apply.
Tank roles are exempt. There is no class inference or default enabling.

A bot already attacking Vek'lor uses the existing raid retarget action to stop
that attack, cancel casts aimed at him and recall/redirect its main pet.
Autonomous target-based damage and pet-attack actions honor the same exclusion.
As with the other raid DPS exclusions, existing DoTs/incidental AoE are not
removed and a pet's defensive AI may reacquire before the next bot reaction.
This assigns physical DPS only: it does not choose caster/pet damage schools,
position tanks, manage teleports or keep the emperors apart. Keep those
assignments under player control; do not enable this strategy on caster DPS.

The shared hazard escape action chooses the nearest area that actually
contains the bot. A nearby smaller area outside the bot no longer masks a
larger containing area. This affects only hazards already supplied to the
existing action; it adds no Venoxis cloud or Jeklik trap discovery, and does
not guarantee a safe final movement route.

### Arlokk panther focus (opt-in)

Enable `.bot command DamageBot all +arlokk panther focus` on damage bots.
On map 309, living combat non-tanks prefer an eligible engaged Zulian Prowler
(15101) using the existing lowest-health, non-CC selection. This also works
while Arlokk is vanished and absent from the candidate list. Explicit attack
orders, raid marks and CC reservations retain priority; tanks keep normal
selection. Disable with `.bot command DamageBot all -arlokk panther focus`.

Arlokk's prowlers repeatedly attack the marked player and remove threat from
their prior victim. This preference assists killing them; it does not promise
stable off-tank pickup or plan safe movement. Missing, distant or ineligible
prowlers remain outside the selector. No mark runout or special healing is
provided, and live encounter behavior has not been verified.

### Optional Geddon Inferno retreat

Enable per bot with `.bot command <BotName> all +geddon inferno`; disable with
`.bot command <BotName> all -geddon inferno`. This is off by default. In Molten
Core, while an engaged Baron Geddon has Inferno or its final-pulse root, the bot
(including the current tank) retreats beyond the 20-yard damage area plus a
safety margin and blocks ordinary chase/charge until Inferno ends. Ranged
attacks and heals remain available from a safe position. It also works while
the bot targets an add rather than Geddon.

Stay, follow, wait-for-attack, grind and an active pull-hold anchor take
precedence: the tactic will not move a bot under those orders. Give the normal
attack order after the pull hold is released when you want the retreat tactic
to take over. An unreachable escape point does not teleport the bot; inspect
its movement and reposition manually. This tactic does not manage pets or
replace Living Bomb handling, and it is not a complete Geddon encounter script.

### Bounded raid bomb runout

Geddon also has a player-designated fixed mode: set `.bot action bombspot`
before combat while standing at the chosen point. It replaces this generic
search for configured MC bots and explicitly overrides parked orders during
bomb escape, waiting and return. See [fixed bomb controls](../play/local-companion-controls.md#fixed-geddon-bomb-spot)
for setup, clearing, distance requirements and Turtle's persistent fire risk.
The generic behavior below remains unchanged for bots without a fixed point.

Living Bomb in Molten Core, Burning Adrenaline in Blackwing Lair and Mutating
Injection in Naxxramas use encounter-scoped runout. The existing Solnius Emerald
Rot tactic shares the same movement. Each aura application keeps its original
search center: candidates extend at most `AiPlayerbot.BombRunoutDistance + 10`
yards from that center, instead of another full runout on every reaction.
The bot checks every live same-instance group member, reuses a safe destination,
and stops requesting runout once everyone is at least the configured distance
away. Ordinary chase, follow and charge cannot take it back while the aura lasts;
casts that need no movement remain available. Solo bots are not held in place.

Candidates must be on the same floor, reachable, outside known hazards and away
from unpulled enemies. With the optional Geddon Inferno tactic enabled, the
checked route also avoids cutting across the active Inferno area. A spread-out
raid or blocked terrain can leave no acceptable destination; the bot does not
expand its search without limit. Stay, combat follow, wait-for-attack, grind and
pull-hold orders still win. Existing emergency ground-effect escape also remains
available and can move beyond the bomb search area. This is not a room-boundary
map or coordinated carrier assignment; validate paths in the encounter.

### Optional Vaelastrasz tank handover

Enable `.bot command <TankBot> all +vael tank handover` on every participating
tank bot; disable with `all -vael tank handover`. The tactic only hands off
Vaelastrasz (entry 13020) when the current live player tank is the boss's victim
and carries Burning Adrenaline (23620 or 18173). The lowest-GUID opted-in,
available tank with a ready taunt is selected. A claimed attempt is retried at
most three times, no sooner than two seconds apart, until a victim change is
observed. Automatic taunts are suppressed while the outgoing tank carries the
aura and while another healthy tank holds the boss, preventing ordinary tank
AI from undoing the handover. Explicit target/raid-mark and hold orders exclude
a replacement.

The existing bounded raid bomb runout handles the aura carrier; this tactic
does not add another movement action or select a drop location. Enable on all
participating tanks so the old tank and other bots respect coordination. If no
eligible replacement can taunt, the current carrier still runs out and the
player must recover the boss manually. Test the actual aura, taunt immunity,
threat switch and boss movement before relying on it; this is a bounded
handover, not a full Vaelastrasz tank rotation.

### Firemaw positioning remains manual

The module has no Firemaw-specific positioning strategy. Keep Firemaw's tank
and raid placement under the raid leader's control; do not expect bots to find
cover or hold a line-of-sight position automatically. The core script casts
Shadow Flame and Flame Buffet on Firemaw and reduces the struck player's threat
by 50% after Wing Buffet. Class tank strategies already react to lost aggro,
but they do not assign a Firemaw tank rotation.

### Broodlord Knock Away and tank recovery

Broodlord's Knock Away (18670) cuts the struck player's threat by 50%. Keep a
designated tank on the boss. If the boss changes to a non-tank, the normal tank
class strategies respond to their lost-aggro trigger: Warrior Taunt, bear
Growl/Faerie Fire, or Paladin Judgement. This is reactive threat recovery, not
a timed two-tank rotation. It does nothing when Knock Away leaves the boss on
the tank, and it does not replace a raid leader's assignment.

### Optional Razorgore phase-one discipline

Enable on the raid bots before the orb/add phase with
`.bot command all all +razorgore phase1`. While enabled, autonomous attacks
and offensive casts against Razorgore are blocked for every role, including
tanks, until the core disables the Orb of Domination after the eggs are
destroyed. The phase check uses the orb's `no interact` flag; if the orb is
temporarily out of sight, the guard stays active. Explicit attack and raid-mark
orders still win. Add pickup, orb operation, and egg destruction remain under
the human operator. The strategy is not automatically enabled by entering BWL;
remove it with `.bot command all all -razorgore phase1` if the encounter resets
or the phase cannot be observed.

### Optional Golemagg DPS target discipline

Enable `.bot command <BotName> all +golemagg dps` on DPS bots; disable with
`all -golemagg dps`. While a live engaged Golemagg is visible in the bot's cached
attackers in Molten Core, autonomous DPS selection skips Core Ragers. Tank
roles, explicit attack orders and the bot's configured attack raid mark take
precedence. No enemies are removed from the shared attacker list: tank pickup,
threat evaluation, healing awareness and CC remain available.

A bot already attacking a Rager switches away and recalls/redirects its main
pet; targeted casts and autonomous pet-attack requests also honor the exclusion.
Existing DoTs and incidental cleave are not removed. A pet's own core defensive
AI can briefly reacquire an attacker before the next bot reaction; use manual
pet control when necessary. This is target discipline, not Golemagg tank
assignment, Magma Splash handling or a complete encounter script.

### Optional Mar'li growing-spawn focus

Enable `.bot command DamageBot all +marli spawn focus` on damage bots. It is
off by default. While an engaged Mar'li (14510) is present in the bot's cached
possible-target list, non-tanks prefer the lowest-health eligible Spawn of
Mar'li (15041) over ordinary boss assistance. The core increases a fighting
spawn's level every three seconds, so leaving eligible spawns alive while
continuing boss damage is costly. Existing DPS assist requests the switch.

Explicit attack targets/raid marks still win; tanks keep their own selection.
Active CC, group reservations, the bot's direct CC target and normal attack
legality remain protected. This uses existing visible candidates only, without
new world scans. It does not assign tanks, interrupt Drain Life, remove existing
DoTs or guarantee a safe route to a spawn. Disable with
`.bot command DamageBot all -marli spawn focus` for normal targeting.

### Optional Thekal phase-one health balancing

Enable `.bot command DamageBot all +thekal balance` on damage bots to prefer
the living, engaged phase-one priest with the most remaining health: Thekal
(14599), Lor'Khan (11347), or Zath (11348). It is off by default. This reduces the
other priests before finishing a nearly dead tank-assist target; the core can
resurrect a fallen priest while another survives. Equal remaining health uses
a stable GUID tie-break. The existing DPS-assist trigger requests target changes.

Explicit attack targets and raid marks still win, tanks retain their targets,
and CC reservations/active CC and ordinary attackability checks remain respected.
The selector uses only cached eligible targets; missing, distant or controlled
priests cannot be balanced. Tank positioning, interrupts, damage already in
flight and DoTs remain manual concerns. This does not guarantee simultaneous
kills or completion inside the resurrection window. Keep the priests accessible
to the damage bots and verify the health balance during a live pull.

The tiger-phase entry (14509) and summoned tigers are excluded. Disable with
`.bot command DamageBot all -thekal balance` for ordinary target selection.

### Optional Jin'do curse cure protection

Enable `.bot command HealerBot all +jindo curse` on each curing bot to preserve
Delusions of Jin'do (24306) on friendly players in Zul'Gurub. It is off by default.
The core rejects shade damage from an attacker without this curse, so removing
it also removes that player's ability to damage the shades. While Delusions is
present, automatic curse removal waits even if another curse would otherwise
trigger the cure. Self/party selection and the final ordinary spell cast share
the guard. Jin'do's Hex is magic and remains eligible for magic dispel; this
strategy does not automate shade targeting, totem destruction or movement.

Disable with `.bot command HealerBot all -jindo curse` to resume normal curse
removal. Explicit custom-cast commands and item use are outside this automatic
spell guard. A cast already in progress is not cancelled. The protection ends
when the exact encounter aura is gone or the target leaves ZG.

### Optional Hakkar poison cure protection

Enable `.bot command HealerBot all +hakkar poison` on each bot that cures.
This is off by default. In Zul'Gurub, while a friendly target carries Hakkar's
Poisonous Blood (24321), the bot declines automatic poison cures on that target.
Blood Siphon checks this aura to damage Hakkar instead of healing him. Another
poison on the same target does not bypass the guard. Cleanse/Purify are also
blocked when triggered by a different dispel type, because their poison effect
would still remove Poisonous Blood. Magic-only and disease-only cures remain
available. The exact map and encounter aura scope the protection; no boss scan
or assumed siphon timer is used. Self and party targets share the same rule.

This protects ordinary spell actions at selection and again immediately before
casting. It does not cancel casts already in progress, existing Abolish Poison
periodic effects or cleansing totems, and it does not intercept items or explicit
custom-cast commands. Manage those separately before acquiring Poisonous Blood.
A new Abolish Poison cast is blocked while the aura is present because its
initial effect also dispels poison. The strategy does not acquire poison, spread
Corrupted Blood (which is not dispellable), or position the raid. Disable with
`.bot command HealerBot all -hakkar poison` to restore normal automatic cures.

### Optional Nefarian phase-three add focus

Enable `.bot command <DamageBot> all +nefarian phase3` on damage bots. Below
20% health, each opted-in non-tank bot with a visible Bone Construct (14605)
selects the lowest-GUID construct among its eligible targets, excluding
active crowd control and targets reserved for crowd control (including moon). Bots with the
same target list choose the same construct. Explicit attack targets and raid
marks keep priority; tanks retain normal target selection. Remove the strategy
with `all -nefarian phase3`. This does not coordinate heals, taunts, or movement.

### Optional Nefarian phase-three add assignment

Enable `.bot command <TankBot> all +nefarian add tank` on participating off-tanks;
remove it with `all -nefarian add tank`. During Nefarian's encounter, opted-in
tanks divide currently engaged Bone Constructs (14605) and Warlock-call
Corrupted Infernals (14668) that each bot can currently target. Existing tank
victims stay assigned, explicit attack targets and raid marks take priority, and
hold orders disable participation. CC marks and reservations are excluded even
before a control aura lands; adds attacking healers or damage bots remain eligible
for tank pickup. Only the participating tanks' cached eligible targets are assigned.
Unassigned adds remain available to normal
target selection. Without the opt-in strategy, target selection is unchanged.

This is add pickup coordination, not a Nefarian class-call controller. Mage
Wild Polymorph dispel response remains in the separate raid-dispel strategy;
other class calls, skeleton pathing and human assignments are not automated.
Confirm the planned assignments during a live phase-three pull before relying
on them.

### Optional Nefarian Infernal focus

Enable `.bot command DamageBot all +nefarian infernal focus` to prefer the
lowest-health eligible Corrupted Infernal while the group tank targets Nefarian.
This single-target DPS preference is off by default and excludes tanks and CC
reservations. Explicit attack orders and attack raid marks retain priority.
When both Nefarian strategies are enabled, phase-three Bone Construct focus
runs first; Infernal focus follows when no construct is selected. Disable with
`all -nefarian infernal focus`.

### Optional Majordomo DPS target discipline

Enable `.bot command <BotName> all +majordomo dps` on DPS bots; disable with
`all -majordomo dps`. Autonomous DPS and pet targeting skip engaged hostile
Majordomo in Molten Core, leaving normal non-CC add selection available. Tank
roles, explicit attack orders and the configured attack raid mark override the
exclusion. This shares Golemagg's retarget and pet cleanup, but each tactic has
its own opt-in. It does not choose an add kill order, manage reflect shields,
assign tanks or automate the surrender/Ragnaros event.

### Optional Garr DPS guard

Enable `.bot command <BotName> all +garr dps`; disable with `all -garr dps`.
During engaged Garr in Molten Core, non-tanks skip unassigned Firesworn, recall
or redirect a main pet attacking one, and suppress autonomous AoE/cleave/chain
and secondary-target DoT actions. Explicit attack targets or the bot's configured
attack mark allow single-target kills. Tank threat actions, healing and dedicated
CC actions such as Banish remain available. Each DPS bot needs its own opt-in.

The guard includes class actions whose existing threat metadata understates
splash: Multi-Shot, Chain Lightning, Cleave, Whirlwind, Thunder Clap, Sweeping
Strikes, Blade Flurry, Carve, Hurricane, Consecration, Holy Nova, Rain of Fire,
Hellfire, damaging fire totems, Explosive Trap and Screech. It does not cancel
already-active cleave buffs, ground effects, DoTs, pet autocast or item procs.
Clear those before pulling and manually control pet autocast. Ordered single
kills still do not authorize autonomous splash. Direct player cast commands
remain explicit overrides. This does not assign banish targets or kite adds:
Turtle's Firesworn gain Separation Anxiety beyond 45 yd from Garr.

### Optional Garr tank assignments

Set one bot's role to tank and enable `all +garr boss tank`.
Set each add bot's role to tank and enable `all +garr add tank`.
For example (replace the names):

```text
.bot role BossTank tank
.bot command BossTank all +garr boss tank
.bot role AddTank tank
.bot command AddTank rti none
.bot command AddTank all +garr add tank
```

Repeat the add command for each add tank. One boss tank and three add tanks
assign eight engaged Firesworn as 3/3/2. Enable only one Garr role per bot.
A human tank already holding Garr reserves the boss role. With no designated
boss tank, one add tank is reserved for Garr. Tank deaths may rebalance adds.

The tanks recover their assigned adds attacking non-tanks before continuing
on held targets, using normal class attacks/taunts. They do not force threat
or guarantee a successful taunt. This does not set tank positions: keep the
adds near Garr (his script applies Separation Anxiety beyond 45 yards).
Only already engaged adds are assigned. Actual/marked CC is respected.

Explicit attack/focus orders take precedence. Bots default to skull focus, so the `rti none` line is required on add
tanks when skull marks Garr. Keep skull for the DPS group. Clear old explicit orders with the normal
stop command before the pull. Stay/follow/wait and pull-hold orders suspend
automatic assignment. Release holds when ready to tank.

Disable with `all -garr boss tank` or `all -garr add tank` on that bot.
The strategies are inactive outside an engaged Garr in Molten Core.

### Optional Onyxia airborne spread for hired bots

Enable `.bot command <BotName> all +onyxia air spread`; disable with
`all -onyxia air spread`. This explicitly permits ranged/healer hires to spread
while Onyxia is airborne, independently of their current target. The ordinary
owned-bot spread exemption elsewhere is unchanged. Tank roles and explicit
stay/combat-follow/wait/grind/pull-hold orders remain exempt.

Start in the central floor area. The tactic searches short steps inside a
conservative ellipse centered at (-25, -215), with 30-yd X and 22-yd Y radii.
Both candidates and checked path vertices must stay inside that area on the
floor; the two egg/whelp sides and entrance are excluded. It checks clearance
from all live same-instance members, stops requesting spread when 10 yd clear,
and limits successful or failed attempts to one per three seconds. Crowding or
blocked paths can leave no destination. Bots outside this region retain normal
movement instead of being automatically led across the egg pits.

While active in the region, ordinary chase/formation/range movement is held;
stationary casts, target selection and emergency hazard escape remain available.
Healers can therefore need manual repositioning to reach distant patients.
Normal movement resumes on landing/reset or disabling the tactic; there is no
forced run back to an old coordinate. This reduces Fireball clustering only.
**It does not identify safe Deep Breath lanes.** Direct movement orders and
emergency hazard escape can leave the bounded spread area.

### Optional Onyxia airborne whelp support

Enable `.bot command <BotName> all +onyxia whelps` on selected melee DPS or an
add tank; disable with `all -onyxia whelps`. During the airborne phase, their
normal DPS/tank target selection prefers engaged, attackable whelps11262,
respecting CC. Add tanks prefer whelps not targeting a tank; melee favors a
nearby current whelp to avoid unnecessary switching. Hired bots are supported.
Ranged/healer roles retain their normal targeting unless explicitly tank-role.

Explicit attack targets, configured attack raid marks and stay/combat-follow/
wait/grind/pull-hold commands override the tactic. Clear a standing boss attack
order or boss attack mark on the selected add-support bots if you want automatic
whelp selection. The boss tank need not enable this mode.

When no whelp is available, ordinary target selection still applies. The tactic
blocks reach/charge toward an airborne Onyxia outside melee reach, while allowing
stationary ranged attacks and reachable melee attacks against Turtle's enlarged
airborne hitbox. All overrides end on landing/reset; normal targeting can keep
finishing a live whelp rather than forcibly abandoning it for the boss. It does
not pull unengaged nest whelps, assign unique whelps across tanks, coordinate
landing threat or provide Deep Breath movement.

### Optional Golemagg boss/add tank assignment

On bots already in tank role, enable `.bot command <BotName> all +golemagg boss tank`
for the intended boss tank and `all +golemagg add tank` for the intended add tank.
Each is off by default and disabled with the corresponding `all -...` command.
One add tank is assigned both Ragers; with multiple opted-in add tanks the Ragers
are divided by stable GUID order. Extra boss-only candidates stand by. Existing
human tank-role players holding/attacking Golemagg retain the boss assignment.

The cached live roster selects an explicit boss holder first, then the lowest-GUID
boss candidate, then one free add candidate as fallback. Dead/offline/different-
instance tanks are excluded. Remaining add candidates retain the Ragers. With
only one tank left, that tank goes to the boss and the adds need manual help.
Explicit attack targets and configured raid marks override automation and reserve
those targets when computing the other bots' assignments. Conflicting explicit
orders remain the player's responsibility. Hold orders also override assignment.

Ordinary tank targeting is filtered to assigned enemies; an existing wrong target
is dropped, and targeted offensive actions cannot immediately undo the assignment.
Tanks retain heals, CC and normal threat abilities. The roster/targets refresh
about once per second and clear on encounter exit. No tank role is changed, no
mob is removed from shared attackers, and this does not position tanks, remove
Magma Splash, guarantee taunt/threat recovery or prevent incidental AoE threat.
A revived preferred tank can regain its assignment; verify transitions manually.

### Optional Ebonroc Shadow handover

Enable `.bot command <BotName> all +ebonroc swap` on the participating tank-role
bots; disable with `all -ebonroc swap`. Keep replacement tanks in a valid stance,
within taunt range and line of sight. When Ebonroc's actual victim has Shadow of
Ebonroc23340, the lowest-GUID ready, unmarked, opted-in tank in the live group is
selected for the next attempt. Normal attack orders/marks on Ebonroc are allowed;
orders to attack another enemy and explicit hold orders exclude that bot.

The replacement must know a usable direct taunt (Taunt, Growl or Hand of
Reckoning with a taunt effect in the current spell data). Readiness checks include
range, cooldown, power/control, stance and taunt-effect immunity. It does not
move tanks into range, change their role/stance, create abilities, or use a pet's
taunt. Missing replacements are a no-op.

A shared instance/boss claim prevents simultaneous attempts: at most three per
victim/debuff application, spaced at least two seconds. Only observing the boss
switch to the selected replacement confirms success; a successful cast call
alone does not. A resisted/failed cast or renewed aggro can use the remaining
budget. Generic automatic taunts/fixates cannot immediately take the boss back
from a healthy tank, and a shadowed tank cannot automatically taunt him back.
Ordinary rescue taunts against a non-tank victim remain possible outside the
Shadow handover. Direct player spell commands remain overrides.

This does not guarantee threat recovery after Wing Buffet, reposition/facing,
a successful resist check, or survival without an available replacement. Natural
threat gains can still return the boss to the old tank. Test multiple complete
Shadow cycles and use manual intervention if the bounded attempts are exhausted.

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

## Loatheb: manual healing and spore rotation

The pinned Classic script applies Corrupted Mind by class; it does not expose a
shared periodic healing window. The resulting override aura makes affected
heals, dispels and certain related spells fail. Assign healing turns manually;
this option does not schedule heals or change healing AI.

Enable `.bot command BotName all +loatheb spore guard` on bots that must leave
spores alone. It is off by default and acts only during engaged Loatheb combat
in Naxxramas. Autonomous DPS selection skips spores, and the retarget action
stops a protected current bot/pet target. Direct attack orders and the bot's
configured attack mark permit a selected spore kill. The guard also suppresses
classified area damage and the existing incidental-damage action list.

Choose the next spore group by hand. Position that group before ordering its
spore kill: spores cast Fungal Bloom on death. The strategy neither moves groups
nor tracks who already received the buff. Existing damage effects, active
totems and pet cleaves are not cancelled; check them before the next spore.
Disable with `.bot command BotName all -loatheb spore guard`.

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

## Naxxramas manual assignments

- **Gothik:** in the speech and balcony add phases, the core resets the fight
  if either side has ten or fewer counted players. During the ground phase,
  at most one counted player on either side makes the gate open instead.
  Counts exclude dead or successfully feigning players, players more than
  100 yards from the combat gate, and players within 0.5 yards of its Y.
  Put more than ten counted players on each side for the early phases;
  standing in the gate does not satisfy either side. Playerbot has no
  side-assignment or closed-gate movement controller. Keep placement and
  tank/healer coverage on each side manual until the gate opens.
- **Noth:** Blink casts Cripple and a teleport, resets threat, then attempts to
  attack a random target. Keep a tank ready to reacquire him after each Blink.
  The script casts Curse of the Plaguebringer on his current victim about every
  50–60 seconds. Normal curse-capable bot dispels now prioritize players with
  Curse of the Plaguebringer29213 in Naxxramas before ordinary curse targets.
  No new toggle is needed. This is target priority, not a guarantee of a dispel
  before its10second Wrath trigger; keep backup decursers in range. Existing
  movement, spell-readiness and Grobbulus injection safeguards still apply.
- **Loatheb:** the core chooses one of two spore locations when the encounter
  AI is constructed and summons a spore every 13 seconds. Playerbot has no
  spore target or location assignment. Keep spore positioning and kills under
  player control; the core comments that the exact spawn pattern is unclear.
- **Faerlina:** the encounter starts with two Followers and four Worshippers.
  Widow's Embrace removes Enrage and delays the next Enrage; it also prevents
  Poison Bolt Volley while its aura is present. Playerbot has no Worshipper
  sacrifice assignment. Choose the Worshipper and timing manually; keep a
  backup plan if the add is already dead or the aura does not land.
- **Instructor Razuvious:** four Understudies are present. The core resets an
  Understudy's threat and applies Mind Exhaustion after its charm ends. There is
  no bot mind-control or Understudy rotation controller. Assign the control
  users and Understudy rotation manually; keep the raid lead ready to recover
  a missed or ended control. Classic control is implemented in the core:
  this is a bot capability gap, not an absent encounter mechanic. Bot pet
  spell actions resolve `GetPet()`, not `GetCharm()`, so enabling pet spells
  does not provide Understudy ability use or a safe charm handoff.
- **Gluth:** the core spawns Zombie Chow, sends each new Chow at a random raid
  target, and makes Chow hit by Decimate run to Gluth. Gluth kills Chow within
  10 yards and heals for five percent per Chow. Playerbot has no Chow kite,
  pickup, or Decimate response assignment. Keep the Chow route and kill order
  under player control, especially when Decimate is near.
- **Kel'Thuzad:** phase one runs for five minutes and twenty seconds before its
  transition; the boss becomes attackable fifteen seconds later. At under 40
  percent, phase three begins and up to five Guardians spawn seven seconds
  apart. The existing `+kt interrupt` option covers its documented interrupt
  task only; it does not assign phase-one waves, spread for Frost Blast, handle
  Shadow Fissure, or assign Guardians. Keep those jobs and phase transitions
  under player control.

## Faerlina Worshipper preservation (opt-in)

Enable `.bot command BotName all +faerlina worshipper guard` on damage bots.
During engaged Faerlina combat in Naxxramas, it protects unassigned hostile
Worshippers from autonomous target selection and many incidental area attacks.
Followers remain ordinary targets. Explicit attack orders and configured attack
marks still allow a selected Worshipper to be attacked. Friendly controlled
Worshippers remain eligible for support spells. Disable with
`.bot command BotName all -faerlina worshipper guard`.

Coordinate control, positioning and Widow’s Embrace casts manually. The pinned
core kills the caster when Embrace is cast; ordinary Worshipper death is not
its recorded EventAI trigger. Embrace removes Faerlina’s Enrage when it lands.
This option supplies no mind-control controller, sacrifice timing or Enrage
reaction. Existing damage effects, totems and autonomous pet splash still need
supervision; use direct orders for the adds assigned to tanks.

## Kel'Thuzad Shadow Fissure (opt-in)

Enable `+kt fissure` in combat/reaction strategies. In Naxxramas combat,
the bot detects live Shadow Fissure creatures (16129) and tries to leave their
seven-yard clearance circle. Void Blast has a five-yard damage radius and
fires about three seconds after the fissure appears. This option can interrupt
the bot's cast to move; it applies to tanks and healers too.

Discovery is shared per bot at most every 500 milliseconds. The reaction trigger
checks once per second; AI delays, roots, crowd control and pathfinding can still
make escape too late. This is not a survival or timing guarantee. Manual stay,
follow, wait-for-attack, grind and pull-hold positions prevent this movement.
Bots with Chains aura 28410 do not use it; the core makes them immune to Void Blast.

The action tries short mesh routes, rejects inward movement through a fissure
and entry into other known hazards, then launches the checked route directly.
An unavailable route leaves movement to player control. The fissure remains a
hazard until its creature despawns, including its brief post-blast lifetime.
Turn the option off with `-kt fissure`.

This does not assign Frost Blast spacing, heal Frost Blast victims, control
Chained players or manage phase-three Guardians. Frost Blast uses a ten-yard
radius in the loaded Classic data: place groups manually with clearance beyond
that radius. Chains can charm the current tank and resets boss threat; assign
a backup tank and controlled-player crowd control manually.

### C'Thun Carapace DPS guard (opt-in)

Enable `cthun carapace` with `all +cthun carapace` on a DPS bot. In AQ40
combat, ordinary target selection excludes C'Thun's body (15727) while it has
Carapace (26156), which reduces damage by 99%. The existing eligible-target
selector can choose a tentacle; it does not force a new target when none is
available. When the aura disappears, normal body targeting is allowed again.
Reapplying the shell reactivates the guard through ordinary AI updates.

The strategy is off by default. Explicit attack orders, the configured attack
raid mark and tank assignments win. The existing retarget path stops the old
attack/cast and recalls or redirects the main pet. Existing DoTs, reactive or
incidental AoE damage are not removed. Eye-of-C'Thun targeting and tentacle
priorities are unchanged. This adds no spread, Dark Glare avoidance, safe
route, stomach assignment or phase timer. Movement still uses the normal AI;
keep encounter positions and stomach roles under player control. Disable with
`all -cthun carapace`.

### Fankriss spawn focus (opt-in)

Enable `all +fankriss spawn focus` on DPS bots to prioritize engaged Spawn of
Fankriss (15630) in AQ40 before assisting the tank's boss target. Spawns enrage
15, 20 or 25 seconds after their wave summons; this option chooses the lowest
absolute-health eligible spawn, without predicting individual enrage timers.

The option is off by default. Explicit attack orders, the attack raid mark,
tank assignments, crowd-control reservations and normal target-validity checks
remain authoritative. Only already-engaged spawns in the cached target list
are considered. Hatchlings are unchanged. The existing retarget action handles
changing attacks and the main pet; this adds no movement or tank-swap logic.
Disable with `all -fankriss spawn focus`.

### Sartura Whirlwind retreat (opt-in)

Enable `all +sartura whirlwind` to treat Battleguard Sartura and her Royal
Guards as moving hazards while their Whirlwind auras are active in AQ40.
The strategy uses the loaded damage-spell radius plus two yards of response
margin, rechecks live positions/auras, and refreshes shared hazard geometry
just before invoking the existing hazard escape action. It suppresses ordinary
approach movement while the bot remains inside that area.

The option is off by default. Combat `stay`, `follow`, `wait for attack`,
`grind` and a set pull-hold position override it, including between trigger
and execution. Existing spells that do not approach the target remain
available; the escape action itself takes emergency priority. Guards are
considered independently of the boss and of the configured attack raid mark.
Only engaged nearby units in the existing possible-target cache are considered.

This reuses generic hazard movement and its path/LOS checks; it does not
predict the boss's pursuit, prove a route safe throughout a moving Whirlwind,
assign tank recovery, spread the raid, or stop pet melee. The boss can change
target every one to two seconds during Whirlwind. Use under supervision;
disable with `all -sartura whirlwind`.

### Skeram image focus (opt-in)

Enable `all +skeram image focus` on DPS bots to prefer engaged Skeram images
over the group tank's target. Images share entry 15263 with the original;
the selector requires summon spell 747 in the unit's creation-spell field.
It does not infer identity from health or use the encounter AI's private flag.
Among eligible images it chooses the lowest absolute health.

Off by default; explicit orders, attack raid marks, tanks, CC reservations
and normal target validity retain precedence. The existing retarget action
changes attacks and redirects the main pet. No platform movement, tank
assignment, charm dispel or automatic crowd control of charmed players is
added. True Fulfillment needs separate handling; this option only focuses
images. Disable with `all -skeram image focus`.

## Ragnaros positioning limits

Wrath of Ragnaros (20566) is centered on Ragnaros. The core resets his threat
when it successfully casts; spreading raid members apart is not a substitute
for managing distance from the boss and recovering tank control. Might of
Ragnaros (21154) is different: it selects a living mana user from the threat
list and summons a Flame trigger for Intense Heat (21155). Do not treat these
as one mechanic or advertise a generic spread toggle as Wrath protection.

Keep tank positions, ranged spacing and knockback recovery under player
control. If no reachable non-GM player remains in melee, the script starts
Magma Blast; moving every melee bot out can therefore introduce another
failure. The room has lava bursts and uneven ledges. Playerbot currently has
no verified Ragnaros safe-position or knockback-landing controller.

Submerge starts after the surface timer and summons eight Sons of Flame.
The boss becomes unselectable, then invisible and friendly during submerge.
He can return early if all remaining living Sons are banished; keep banish
assignments and return timing under player control. An add-target priority
alone would not supply a safe route through the lava or an assigned taunt.

### Ragnaros Sons pickup (opt-in)

Enable `all +ragnaros sons` on selected add tanks and damage bots. During
submerge, this prioritizes engaged, uncontrolled Sons actually summoned by
Ragnaros. Tanks prefer Sons not currently held by a tank. The boss is resolved
through the summon owner even while submerged and invisible; phase detection
does not require him in the normal attacker list. Healers do not opt into
attacking unless also assigned a tank role.

Explicit targets, raid attack marks and stay/follow/wait/grind/pull holds win.
Banished and other crowd-controlled Sons are skipped. The action uses ordinary
attack handling; it does not assign a taunt, route around lava, schedule banish
or guarantee immediate reacquisition. Priority ends when the boss emerges;
leftover Sons can still be fought through ordinary defensive targeting.


### Kurinnaxx Mortal Wound handoff (opt-in)

Enable `kurinnaxx swap` with `all +kurinnaxx swap` on at least two tank bots
in the same group. In AQ20, a participating victim with three or more Mortal
Wound stacks (25646) can hand off to a participating tank with no stacks and
a currently usable taunt, already targeting Kurinnaxx. The lowest-GUID eligible
volunteer claims the attempt;
resisted/failed attempts are limited to three, at least two seconds apart per
victim/debuff application. Keep tanks positioned within taunt range yourself.

After another participating tank becomes the victim, a wounded tank stops its
boss attacks and targeted casts until its own wound expires. New boss attacks,
offensive casts and damaging AoE are held; healing and defensive self spells
remain available. Generic taunts cannot race the coordinated handoff. Existing
DoTs, reactive damage and pet AI are not cancelled. This is not a threat wipe.

The strategy is off by default and does not automate human tank handoffs.
Explicit attack orders, an attack raid mark on another target, and stay/follow/
wait-for-attack/pull-hold orders suspend participation. Removing the strategy
with `all -kurinnaxx swap` restores ordinary behavior. If no clean volunteer
can taunt, or all attempts fail, the current tank keeps fighting; intervene
manually. Sand Trap movement and encounter positioning are outside this policy.

### Princess Yauj proactive Tremor (opt-in)

Enable `all +yauj fear` on shamans to request a known, ready Tremor Totem while
their current enemy target is Princess Yauj (15543) in AQ40. The core uses
Panic (19408) for her fear. Existing totems, spell readiness and explicit
stoneclaw/stoneskin/earthbind/strength earth-totem orders remain authoritative.
This requests the existing Tremor action; it does not guarantee pulse timing,
protect other subgroups or find Yauj when the bot targets someone else. Priests
already have generic tank-first Fear Ward selection; this adds no priest rule.
Disable with `all -yauj fear`. Off by default.

### Kri Toxic Vapors (opt-in)

Enable `all +kri cloud` to retreat from a live Toxic Vapors object left by
Lord Kri (15511) in AQ40. The existing nearby-corpse cache finds Kri; the
handler reads spell 25786's actual object position and radius, with a two-yard
response margin. No guessed cloud lifetime or summoned-creature ID is used.
It refreshes hazards before escape and pauses competing approach actions while
inside. Healing and emergency escape actions remain available.

Off by default; disable with `all -kri cloud`. Current-state stay, follow,
wait-for-attack, grind and pull-hold orders take precedence. It can work after
combat if those holds are absent, but ordinary out-of-combat follow still wins.
The corpse cache refreshes every two seconds and requires line of sight; this
is not immediate detection or a guarantee of avoiding damage. Four seconds after an early Kri kill, the core removes and relocates his
corpse; this is not necessarily world removal or cloud expiry. If the relocated
corpse falls outside the cache's range/LOS, detection may cease while the cloud
remains. No new grid scan, core seam or invented persistent hazard is added.

### Ayamiss larva rescue (opt-in)

Enable `all +ayamiss larva focus` to prioritize an eligible Hive'Zara Larva
(15555) in AQ20 when its current victim is a living, paralyzed (25725) member
of the bot's group. Tanks keep their existing target assignments. Explicit
attack orders, raid marks and crowd-control reservations retain priority.
The selector uses the existing attack-target cache and normal target checks;
it does not inspect private script state or scan the map for victims.

Off by default; disable with `all -ayamiss larva focus`. The larva must already
have acquired its victim. This does not interrupt a current cast, teleport a
bot to the altar, free Paralyze directly or guarantee arrival before Feed.
The existing target-switch action handles an eligible change, including pets.

### Collapsed Core Hounds in Molten Core

Bots skip collapsed Core Hounds (entry 11671), including a collapsed marked or
explicitly ordered target, and can select the remaining live enemies. A hound
that revives becomes eligible again. No extra strategy or command is needed.
This does not synchronize pack damage: the group must still finish the hounds
within their revival window. Ancient Core Hounds and Core Ragers are unchanged.


### Combined upstream and local raid tactics

The opt-in local tactics keep ownership of their overlapping decisions when
both strategy families are present: `geddon inferno` keeps its 23-yard
combat clearance and rooted final pulse; `onyxia deep breath` uses the full
spell-chain clearance checks; `ossirian crystal` keeps the elected nearby
clicker and bounded retries; `thaddius polarity` keeps its checked side paths;
and the Golemagg boss/add tank strategies keep their assignments without
slot-order camp movement. Disabling the local strategy restores the upstream
automatic tactic. Upstream Geddon movement suppression also permits local
Inferno retreat, fixed bomb-spot movement, and hazard escape. Air-spread
suppression permits upstream Onyxia breath escape when that tactic is used.

The combined policy tests retain both upstream and local cases. Production
trigger fixtures check that enabling a local tactic suppresses only its
corresponding upstream trigger and disabling it restores that trigger.

### C'Thun sector spread

The optional `cthun spread` strategy adds surface entry and sector spacing
to the separate `cthun carapace` target guard. See
[player controls](../play/local-companion-controls.md#cthun-spread-optional) for the macro,
raid-group arrangement, movement precedence and limitations. It uses the
fixed 1/2/2 slots in eight 45-degree sectors from the raid chart, reachable
local add selection, and the actual small-eye birth coordinates in Turtle's `boss_cthun.cpp`. It does not implement Dark Glare
or stomach tactics. A passing geometric layout fixture does not establish
that a live raid has safe routes, suitable group composition or a clear.
