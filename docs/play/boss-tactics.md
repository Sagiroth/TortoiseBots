---
id: play-boss-tactics
title: Boss Tactics Table
category: play
summary: The per-boss tactics table (ai_boss_tactic), the stand sets (ai_boss_stand) and the opt-in boss tactics strategy that applies them.
tags: [guide, dungeon, boss, tactics, uldaman, configuration, commander]
relates_to:
  - play-dungeons-and-raids
  - server-configuration
  - developers-strategy-engine
  - developers-architecture
---

# Boss Tactics Table

> Work in progress: this feature is still being built and tested.

A boss fight has a few mechanics that generic combat cannot know: which spell to interrupt, which add to kill first, where not to stand. This page describes the data table that holds them and the strategy that applies them.

It is **off by default**. A normal player's bots play exactly as before.

## When it is active

The `boss tactics` strategy is added to a bot by the `enter boss tactics` trigger of the `dungeon` strategy (re-checked every 5 ticks) when all of these hold:

- the bot is on a map that has a row in `ai_boss_tactic` OR `ai_boss_stand` (a stand set alone arms it), and
- either `AiPlayerbot.DungeonTactics = 1` (server owner opt-in; default `0`), or the bot is in a bound group:
  - a remote bind needs `AiPlayerbot.RemoteCommander = 1`,
  - an `inparty` bind needs `AiPlayerbot.DungeonSelfClear = 1`.

`leave boss tactics` removes it when the bot is on a map without rows (not while teleporting or loading), or when the key is off and the bot is no longer in a bound group. The gate is `BossTacticsWanted`.

A boss that vanishes mid-fight (invisible and off the bot's attackers list while alive and in combat, like Arlokk) keeps its rows for up to 90 s as long as the bot still has attackers and the boss stays within 60 yd and out of evade mode (`BossPresenceMemory`), so rows for its adds, such as the Zulian Prowler `kill_first`, stay active during the vanish. The rows end with the fight, the boss's death or evade, or the grace window.

Commander-bound groups use it because commanders cannot give interrupt and kill orders fast enough. The key stays `0` because it changes how bots play.

Two extra opt-ins:

- `linked_death` needs `AiPlayerbot.DungeonRunLinkedDeath = 1` (default `0`). Rows exist but are unread while it is off.
- Aura rows need `AiPlayerbot.DungeonRunAuraTactics = 1` (default `0`). These are `dps_hold_after`, `dispel_priority`, `flee_on_aura`, `freeze_on_aura`, `taunt_after_cast`, `swap_on_aura`.
- An `avoid_cast` row in mode `all` needs `AiPlayerbot.DungeonRunAvoidCastAll = 1` (default `0`): with it off the row holds a tank the caster is on like a default row.
- `.reload config` re-reads these keys without a restart.

## The table

`ai_boss_tactic` is created and filled by `data/sql/world/*_world.sql` migrations. It is read at startup into a cache, one pass, nothing per tick.

- `ai_boss_tactic` is also re-read by `.bot run reloadplans` (no restart needed for it).
- `ai_boss_stand` is startup-only (restart needed).

| Column | Meaning |
| :--- | :--- |
| `map` | Map id. |
| `boss_entry` | Creature entry whose presence among the bots' attackers activates the row. |
| `kind` | See Kinds below: `interrupt`, `kill_first`, `behind`, `avoid_cast`, `tank_hold`, `hold_kill`, `one_at_a_time`, `linked_death`, `seek_aura`, `pull_gate`, `spread`, `use_object`, `threat_successor`, `anchor`, plus the aura kinds behind their own key (`dps_hold_after`, `dispel_priority`, `flee_on_aura`, `freeze_on_aura`, `taunt_after_cast`, `swap_on_aura`). |
| `target_entry` | The creature the row is about; `0` means the boss itself. `hold_kill` is inverted: `boss_entry` is the spared entry and `target_entry` is the blocker. `one_at_a_time` uses target `0` and `boss_entry` is the paced entry. `linked_death` lists one partner per row and `boss_entry` is the anchor. A `seek_aura` row names the source creature whose death leaves the cloud; a `pull_gate` row has `target_entry` 0 and names the boss itself, as does a `spread` row. |
| `spell_id` | `interrupt` / `avoid_cast` spell. `seek_aura`: the aura a bot must carry; `pull_gate`: the aura the boss must not wear; `spread`: the splash the spacing answers (documents the row only). |
| `priority` | `kill_first`: lower dies first. `hold_kill`: budget, how many blockers may live before DPS stop (`0` = not one more). `linked_death`: group id. `seek_aura`: the health % at which DPS stop on the source while the gather is not ready; `pull_gate`: most stacks allowed (`0` = none); `taunt_after_cast` with mode `until_tank`: which raid tank takes the subject. `anchor`: the tanks' radius in yards (`0`: the shared `param`). |
| `param` | `behind`: yards of front kept clear. `avoid_cast`: yards left. `linked_death`: hold percent, 0-100 exclusive of 0. `dps_hold_after`: hold seconds, 0-30. `seek_aura`: gather radius around the source; `pull_gate`: seconds the run leader holds before ending the run (1-300); `spread`: yards the ranged raid keeps apart (5-30); `taunt_after_cast`: window seconds (0 means 10). `anchor`: the circle's radius in yards for everyone, 10-60. |
| `tactic_mode` | Only some kinds read it. `until_tank`: a `taunt_after_cast` row assigns a tank by raid order while the subject stays off a tank, and opens a new window every `param` seconds until a tank has it. `all`: an `avoid_cast` row clears the tanks too while the listed spell is cast (needs `AiPlayerbot.DungeonRunAvoidCastAll = 1`), instead of letting a tank the caster is on hold it. |
| `window_s` | `linked_death`: burn window seconds (default 7). `seek_aura`: dose window opens this many seconds before the cycle is due. `0` for every other kind. |
| `cycle_spell`, `cycle_s` | `seek_aura` only: boss spell whose aura restarts the cycle clock, and the cycle length in seconds. `0` for every other kind. |
| `spot_x`, `spot_y`, `spot_z` | `seek_aura`: where the source is held and killed. `anchor`: the circle's centre (the boss's home). `0` for every other kind. |
| `burn_order` | `linked_death` only: kill order once released. First non-empty wins. |
| `hp_min` / `hp_max` / `bot_aura` / `mode` | Row gates, where read (never on `seek_aura`, `pull_gate`, `spread` or `anchor`). |
| `comment` | Free text. |

The true primary key is `(map, boss_entry, kind, target_entry, spell_id, hp_min, hp_max)`.

The loader skips unusable rows and logs the count: unknown kind, interrupt without spell, behind without radius, avoid without spell or radius, tank_hold with target_entry, hold_kill without target_entry, one_at_a_time with target_entry, linked_death without target_entry or param outside (0,100], `seek_aura` without target_entry, spell_id, cycle_spell or spot, param outside (0,10], priority above 99, cycle_s outside (0,600] or window_s outside (0,cycle_s), `pull_gate` with target_entry, without spell_id or param outside (0,300], `spread` with target_entry or param outside 5-30, `threat_successor` with target_entry or a gate, `anchor` with target_entry, spell_id or a gate, without a spot, param outside 10-60 or priority above param, a cycle/spot/mode column the kind does not read, duplicate key.

### Kinds

| Kind | What bots do | Who |
| :--- | :--- | :--- |
| `interrupt` | A bot with a ready interrupt in range uses it: Pummel, Shield Bash, Kick, Counterspell, Earth Shock, first castable wins. Class interrupts on the current target are unchanged. Generic slot then channel slot, wider radius wins. | Any bot with an interrupt |
| `kill_first` | Takes the listed creature before anything else. Lowest priority first, then a creature whose victim is a healer of ours (a Shade of Jin'do on a healer comes before one on a damage dealer), nearer breaks the rest of the tie, nothing past 40 yd. Generic assist and AoE do not pull back. An explicit attack order wins, with one exception: a bot carrying a row's `bot_aura` (Delusions 24306) turns on the nearest live unit of that row within its reach (melee reach for melee, 30 yd for ranged) even under an order or the run's focus, and the run's focus skips it while it does; once the aura is gone it returns to normal targeting. | DPS only, never tanks or healers |
| `hold_kill` | DPS leave `boss_entry` alone while more than `priority` living blockers stand within 50 yd. Pickers skip the held entry. A bot already on it takes the `kill_first` pick if there is one, else the nearest attackable within 40 yd, else stops. The grid search counts a fighting or blocked creature, so the portal counts while an unwoken add does not. | DPS |
| `one_at_a_time` | All DPS kill one living unit of the entry at a time: lowest-health fighting unit, lowest guid breaks ties. CC and evading units are never picked. | DPS |
| `linked_death` | The group must die together. Free DPS take the highest-health-fraction living member within 50 yd. A member at or below `param` % is held: never picked, AoE leaves it out, area and DoT casts are vetoed. Burn order applies once released. Release latches with `window_s` once armed (5 s, default 7 s); evade resets it. | Free DPS |
| `behind` | Within `param` yd in the front half, step 120 degrees off the boss facing at the same distance. | Everyone except tanks and the bot the boss attacks |
| `avoid_cast` | Within `param` yd 3D of the caster (channel slot included), walk to 2 yd past the radius on ring points, shortest walk first. Listed blasts hold reach, chase, heal, follow, and stay-back moves until the cast ends or the caster dies. A row in mode `all` also clears the tanks while its spell is cast (no tank holds it); with `AiPlayerbot.DungeonRunAvoidCastAll = 0` the row holds like a default row. | Everyone the row covers (tanks included for `all`) |
| `tank_hold` | Opt-in per caster, `target_entry` 0. A tank whose target is that caster stays in the blast, keeps it, and is the preferred pickup. Only its own caster's blast, and only while that caster is on a non-tank; the pickup outranks the raid mark when nearer. | Tanks |
| `dps_hold_after` * | Hold DPS after the trigger, `param` seconds. | DPS; needs `DungeonRunAuraTactics = 1` |
| `dispel_priority` * | Ranks aura removal. | Dispensers; needs `DungeonRunAuraTactics = 1` |
| `flee_on_aura` * | The carrier leaves the group. | Carrier; needs `DungeonRunAuraTactics = 1` |
| `freeze_on_aura` * | The carrier holds all actions but freeze. | Carrier; needs `DungeonRunAuraTactics = 1` |
| `taunt_after_cast` * | Taunt on the trigger. Mode `until_tank`: while the subject stands on a non-tank, the row's `priority` tank by raid order taunts it back, and a new window opens every `param` seconds for as long as it stays off a tank. One tank holds a window until it ends or that tank can no longer take a taunt back (death, a charm, a stun, a hex, a fear, a silence, a polymorph); its taunt being on cooldown after its own cast never hands the window on, so one window burns one taunt; a tank that can take the taunt now (in melee of the subject, targeting it, not its victim, in control, not silenced or polymorphed, its taunt off cooldown) is picked before one that can cast its taunt at all, and that one before a tank that cannot cast now, so a Hexed tank never takes a window from a healthy one; a taunt that was cast and still failed hands the next window to the next such tank. `spell_id` 0 is legal for instant charms. A row keyed to an instant teleport (`spell_id` 24466, Jin'do's Banish) also opens when the subject's tank victim teleports: the spell leaves no aura and no cast to read, so the trigger is the tank landing at the spell's destination (or its position jumping more than 20 yd between two reads). The window is latched while the subject follows the tank, and closes as regained when another tank has the subject, the teleported tank is back within 10 yd of it, or after 12 s. A tank that merely stands far away opens nothing. Only mode `until_tank` does anything: a `blink` row, or a spell-keyed row in any other mode, is accepted by the loader and ignored. A creature-charmed tank only leaves the order while `AiPlayerbot.DungeonRunCharmYield = 1` is also on. A subject the game makes immune to taunts opens no episode at all: no taunt of that tank can land on it, so it keeps its attack and its taunt for a target that can be moved, and the server log says `taunt_after skipped: target taunt-immune` once per fight for that subject. | Tanks; needs `DungeonRunAuraTactics = 1` |
| `swap_on_aura` * | Tank rotation. | Tanks; needs `DungeonRunAuraTactics = 1` |
| `seek_aura` | The raid doses itself from a dying source before the boss's cycle spell lands. The first siphon is timed from the fight's combat start, so cycle 1 gets the same dose window as every later one. While the dose window is open (`window_s` seconds before due, and up to 8 seconds past it when a sighting is missed, after which the cycle estimate rolls forward one cycle at a time (the grace stays under the gap between a cycle and its window, so the rolled cycle's window opens soon after)), bots lacking `spell_id` walk to the living source and stand within `param` yd; DPS bring it to `priority` % and stop until the gather is ready, then burn it so the cloud lands on the raid. A Son that is not fighting yet is fetched for real: the second off-tank walks to it (moving itself, not only setting a target) and hits it until it joins the fight, then drags it to the spot, and it keeps after that Son while the Son stays within 90 yd of the spot and for up to 20 seconds even after it leaves the 60 yd pickup radius; with fewer than three tanks alive (exactly two) the raid's nearest hunter (else a ranged damage dealer) opens a Son within 35 yd of the spot with one shot from its ranged weapon (the auto shot or a wand, never a damage over time whose ticks would run through the hold) and the first off-tank takes it over; a dead, charmed or boss-targeted member is never that puller. The source search reaches the spot's radius while three tanks are there, and 35 yd once the burn is forced, a source inside that reach already fights at or below the hold percent, or any source inside it is already fighting: that source becomes the gather and the kill pick wherever it stands. The only held hit that is let through is the puller's own strike on the Son its pull fetched, from the pull's start until that Son is dragged in (a DoT is still vetoed there). A Son that only joins the fight with the siphon due within 30 seconds is burned at once instead of waiting for the forced burn. A Son that fights a healer or another non-tank for 8 seconds is taunted off by the first off-tank when the third off-tank is dead or already holds a Son of its own. Only the picked source may be damaged, and only inside the window — every other Son stays held, and with the window shut the pick is left at whatever health it has so it serves the next siphon. An area cast that reaches a held Son is vetoed even when the picked Son beside it may burn, a Son that is not fighting is never picked, and once the whole raid carries the aura nothing is killed until the next siphon strips it. A damage-over-time with no direct part (Corruption, Serpent Sting) and a channelled one (Mind Flay, Drain Life) never goes on a Son, since its ticks would run through the hold; a nuke that also ticks (Fireball, Immolate, Moonfire) is allowed only on the Son the hold lets burn. The off-tank holding the Son stops auto-attacking it at or below the hold percent plus 10 while the window is shut, and keeps the threat. Hunters keep the siphon cycle: the pet is dismissed from 5 s before the due time until 2 s after it (and while the siphon's aura is on the bot) and called back afterwards, because an undosed pet is a full heal of its own. A warlock dismisses its demon once when Hakkar is engaged and summons nothing during the fight — a new demon costs a soul shard and a 10 s cast — and never an Infernal or a Doomguard. A shaman unsummons its totems for the same window and places none, since each totem is one more undosed unit. Needs `AiPlayerbot.DungeonRunSeekAura = 1` (default `0`); main tank and first off-tank never seek. | every bot without the aura except the two tanks; second off-tank pulls and holds the source (a hunter pulls when it is not there); pet owners send the pet away |
| `pull_gate` | A run-leader gate, not a fight scan: while the boss wears `spell_id` above `priority` stacks the run leader never pulls it and notes the gate; after `param` seconds it ends the run. Needs `AiPlayerbot.DungeonRunPullGate = 1` (default `0`). | the run leader |
| `spread` | While the boss fights, the ranged raid keeps `param` yd apart (the spell only documents the splash); a bot on a stay spreads too. Needs `AiPlayerbot.DungeonRunBossSpread = 1` (default `0`). | ranged bots and healers; melee and tanks never spread |
| `needs_aura` | Only a bot carrying `spell_id` may target the unit; with `AiPlayerbot.DungeonRunAuraGatedTargets = 1` other bots leave it alone. | Any bot with the aura |
| `use_object` | One claimed bot clicks the boss's game object at the right moment; mode `tank_anchor` also walks the boss over to it. Needs `AiPlayerbot.DungeonRunUseObject = 1` (default `0`). | Claimed clicker (free damage, healer or non-tank) |
| `burn_mana` | A few eligible bots (no tank, no healer, a Mana Burn priest, a Drain Mana warlock or a Viper Sting hunter; `priority` of them, `0` = all, ordered by guid) drain the boss's mana while it stands at or above the row's start % (`param`); only members that run the boss tactics and can cast right now (in combat, in control, their spell off cooldown, the boss in their spell's own range, enough mana) take a slot, a global cooldown does not cost one, and the burn pauses while the boss wears the stone-phase aura 25685. `target_entry` is 0, `spell_id` is unused. Needs `AiPlayerbot.DungeonRunBurnMana = 1` (default `0`). | Eligible drainers; the rest of the raid keeps its rotation |
| `threat_successor` | The boss itself (`target_entry` 0): its damage dealers are measured against the highest threat among the assigned off-tanks instead of the main tank, so the off-tank stays second and a threat reset (Hakkar's Cause Insanity) hands the boss to it instead of to a damage dealer. An off-tank counts as the reference once it carries more than a tiny absolute amount (1 % of the highest tank's threat, or 100 threat when that is larger), so a barely engaged or just-reset off-tank cannot stop the damage while one that is merely behind on damage still takes the boss after a threat reset. Only the boss's own data turns this on; most raid bosses are taunt-immune but keep the highest tank as the reference. Read straight from the table, so the kind takes no gate and no mode word. | Damage dealers of the group |
| `anchor` | The boss itself (`target_entry` 0, no spell): while the boss fights (one of the side's attackers, alive, in combat, not evading), every movement destination the boss and raid code picks for a bot of the fight stays inside a circle around `spot_x/y/z` (the boss's home). `param` is the radius for everyone (10-60 yd), `priority` the tanks' own radius (0: the shared one, never wider). A destination outside moves onto the circle toward it, at the floor there; a point whose floor is more than 4 yd off the centre's height is skipped for that tick. The movers it covers: the `seek_aura` gather, hold and cloud walk, the seek tank's pull walk and drag to the spot, a stand-set seat, the raid spread step (a step that would leave the circle tries the next heading), the tank's face-away sidestep, the walk back to a stay point (a landed pullback's anchor included), every chase (a hostile farther than the radius plus 5 yd is not chased at all), and the run leader's approach-stand ring for that boss's step (ring points outside are no stand). The tank's seek pull only walks to a source within its radius plus 5 yd and within 4 yd of the centre's height. When none stands there, the ranged fallback (a hunter, else a ranged damage dealer, whatever the tank count) shoots one from the circle's point toward it, within its spell range and in sight, and the source walks up to the raid (`seek pull: ranged from anchor`); with no such source, one ranged damage dealer walks out for it (below). A seeker ignores a cloud wholly outside the circle. The off-tank's take of a source outside the circle drops the chase and taunts from where it stands. No key of its own: a boss without the row is unchanged. | Every bot of the fight |
| `tank_ignore` | While the boss fights (a unit of `boss_entry` alive, not evading and engaged with the group within 40 yd), no tank picks, guards, engages or taunts a unit of `target_entry`: the attack pickers refuse it to a tank (so no tank target, attacker-of-non-tank or snare taunt), the `taunt_after_cast` scan skips it, and the run guard (`GuardPick`, the off-tank pick-ups, the engage on arrival) sends no tank at it. The damage dealers kill it through the normal kill logic, and the raid healer guard still sends its ranged damage dealers. Read straight from the table: `target_entry` only, no spell, no gate, no mode word. Data-gated, no config key. | Tanks |
| `ranged_only` | Melee bots (melee damage and tanks) leave the subject alone while it stands `param` yd (3 to 40) or more above them, and take the adds; a ranged bot is never held, an explicit attack order beats the hold, and the hold ends by itself when the subject lands. `target_entry` 0 is the row's boss. Needs `AiPlayerbot.DungeonRunRangedOnly = 1` (default `0`). | melee damage and tanks; ranged bots, healers and an ordered bot keep the subject |

\* Needs `AiPlayerbot.DungeonRunAuraTactics = 1`.
Notes: sole and linked picks use the fight-wide cached grid search, never the bot's own attacker list. A held member vetoes area and DoT casts within 10 yd (`kHoldKillAoeNearYd`), even for an explicitly ordered non-tank or healer, and tanks' own area actions and passive cleaves are vetoed near a held member while ticking DoTs keep ticking. The raid mark falls through a held member to the equalise pick.

Not yet covered: re-grabbing the boss after a threat wipe (the `taunt_after_cast` kind exists, but Ironaya's *Knock Away* has not had its live tuning pass), dispel priorities and ground-effect avoidance. The generic raid hazard and dispel code covers part of it.

## Shipped rows

Boss name first, then the rows that fire for it. No row means the boss has no tactics yet.

### Uldaman (70)

| Boss | Rows |
| :--- | :--- |
| Revelosh | `interrupt` Chain Lightning |
| Baelog, Eric, Olaf | `kill_first` order Baelog (1), Eric (2), Olaf (3) |
| Ironaya | `behind` 10 yd (Knock Away unhandled) |
| Obsidian Sentinel | `kill_first` Shard |
| Grimlok | `interrupt` Lightning Bolt 9532 |

### Deadmines (36)

| Boss | Rows |
| :--- | :--- |
| Voss | `interrupt` Poison Cloud |
| Harvester | none |
| Greenskin | `interrupt` Poisoned Harpoon |

### Wailing Caverns (43)

| Boss | Rows |
| :--- | :--- |
| Zandara | `interrupt` Chain Lightning, Gust of Wind |
| Vangros | none |
| Serpentis | `interrupt` Healing Touch, Sleep, Lightning Bolt |
| Cobrahn | `interrupt` Druid Slumber, Lightning Bolt, Healing Touch |
| Pythas | `interrupt` Healing Touch, Sleep, Lightning Bolt |
| Skum | `interrupt` Chained Bolt |
| Mutanus | `interrupt` Naralex Nightmare |

### Frostmane Hollow (822)

| Boss | Rows |
| :--- | :--- |
| Kan'za | `interrupt` Frostbolt |
| Tan'sha | none |
| Hailar | `interrupt` Frostbolt |
| Ubukaz | none (Ritualists unhandled: passive, never in attacker list, needs a code change, no row) |

### Windhorn Canyon (820)

| Boss | Rows |
| :--- | :--- |
| Stormhoof | `interrupt` Corruption |
| Shalk | `interrupt` Flame of Shalk |
| Vortalus | `interrupt` Gust of Wind |
| Ahgk'tos | none (no covered cast) |
| Pathun | none (no covered cast) |
| Narlgom | none (no covered cast) |
| Walgan | none (no covered cast) |
| Rotag | none (passive and immune, never targetable) |

### Gilneas City (815)

| Boss | Rows |
| :--- | :--- |
| Greymane | `interrupt` Fear |
| Ragetooth | `interrupt` Howl of Terror, Shadow Bolt |
| Sutherland | `interrupt` Smite, Mind Blast, Fireball |
| Greystone | `interrupt` Holy Fire |
| Holtz, Dustivan, Mortimer | none |

### Karazhan Crypt (800)

| Boss | Rows |
| :--- | :--- |
| Enkhraz | `interrupt` Fear, Freeze |
| Corpsemuncher | `interrupt` Acid Spit |
| Andreon | `interrupt` Mind Blast |
| Marrowspike, Hivaxxis, Gort | none |

### Crescent Grove (802)

| Boss | Rows |
| :--- | :--- |
| A'lathea | `interrupt` Starfire |
| Ranathos | `interrupt` Wrath, Entangling Roots, Regrowth |
| Raxxieth | `interrupt` Shadow Bolt, Flamestrike |
| Engryss, Fenektis | none |

### Zul'Farrak (209)

| Boss | Rows |
| :--- | :--- |
| Velratha | `interrupt` Healing Wave, Shadow Bolt Volley |
| Sezzziz | `interrupt` Heal, Renew |
| Antusul | `interrupt` Flash Heal, Healing Ward of Antusul, Healing Ward; `kill_first` Servant |
| Ukorz, Ruuzlu | none (cleaves, no yard value) |
| Zumrah ward | none (totem never in attacker list) |
| Gahz'rilla | `interrupt` Icicle 11131, Freeze Solid 11836 |
| Zumrah, Theka, Nekrum | no row |

### Scholomance (289)

| Boss | Rows |
| :--- | :--- |
| Vectus | `kill_first` Marduk; `interrupt` Flamestrike |
| Marduk | `kill_first` self (Marduk's party-wide shadow first) |

### Zul'Gurub (309)

| Boss | Rows |
| :--- | :--- |
| Bat Rider | `avoid_cast` Unstable Concoction 13 yd (no interrupt: uninterruptible) |
| Voidwalker | `avoid_cast` Consuming Shadows 8 yd; `tank_hold` row |
| Mad Servant | `interrupt` Flamestrike; `hold_kill` budget 0; `one_at_a_time` |
| Gurubashi Berserker (11352) | `taunt_after_cast` mode `until_tank`, tank 1, 5 s (Knock Away 11130 and Intimidating Roar 16508 both reset threat; the first off-tank takes it back while it stands on a non-tank). Needs `DungeonRunAuraTactics` and `DungeonRunCharmYield` (both default `0`). |
| Venoxis | `interrupt` Holy Fire; ranged and healer stand set |
| Thekal, Lor'Khan, Zath | `linked_death` group, hold 2 %, burn order Zealots then Thekal; Lor'Khan `interrupt` Greater Heal |
| Hakkar the Soulflayer | `dps_hold_after` 5 s from the pull (no spell, mode `engage`); `seek_aura` Poisonous Blood 24321 on a Son of Hakkar, hold at 30 %, gather 4 yd at (-11787.4, -1664.3, 54.0); `pull_gate` on Hakkar Power 24692, 0 allowed, hold 30 s; `spread` 12 yd on Corrupted Blood 24328; `taunt_after_cast` mode `until_tank`, tank 1, 10 s (inert: Hakkar is taunt-immune, the row opens no episode); `threat_successor` (the damage dealers' threat cap is the assigned off-tanks' highest); `anchor` 30 yd around his spawn, tanks 25 yd. Needs `DungeonRunSeekAura`, `DungeonRunPullGate`, `DungeonRunCharmYield` and `DungeonRunBossSpread` (all default `0`), plus aura tactics for the taunt row. |
| Jin'do the Hexxer | `needs_aura` Shade of Jin'do 14986 on Delusions 24306 (only a carrier targets it; its carrier turns on it under the run's focus, and healers weight up the member a Shade attacks); `kill_first` Shade (gated on 24306), Powerful Healing Ward 14987, Brain Wash Totem 15112; `dispel_priority` Delusions `never`; `tank_ignore` Sacrificed Troll 14826 (the damage dealers kill the trolls, the tanks stay on Jin'do); `threat_successor` (he is taunt-immune, so the damage dealers stay below the off-tank). No `taunt_after_cast` rows: his taunt immunity made both (Hex/charm and Banish 24466) useless, and 20261106120000 deleted them. Needs `DungeonRunAuraTactics` and `DungeonRunAuraGatedTargets`. |
| Arlokk | `kill_first` Arlokk herself (1), then `kill_first` Zulian Prowler (2); `taunt_after_cast` mode `until_tank`, tank 1, 3 s on her Ambush/Gouge; `dps_hold_after` on panther transform 24190, 2 s. Needs aura tactics only (no charm key: the wipe is Vanish, not a charm). |

## AQ20 rows (map 509)

Only kinds the module already runs. Needs `DungeonRunCharmYield = 1` on the two `until_tank` rows, `DungeonRunTankSwap = 1` (default `0`) on the Kurinnaxx row, `DungeonRunBurnMana = 1` on Moam's `burn_mana` row and `DungeonRunRangedOnly = 1` on Ayamiss's `ranged_only` row.

| Creature | Row |
| :--- | :--- |
| Kurinnaxx | `swap_on_aura` Mortal Wound, 5 stacks, 2 tanks |
| Ossirian | `use_object` crystal; `taunt_after_cast` mode `until_tank`, tank 1; `dispel_priority` Curse of Tongues |
| Moam | `kill_first` Mana Fiend; `burn_mana` from 10 % mana, 4 burners (paused during the stone phase, aura 25685) |
| Ayamiss | `kill_first` Hornet; `taunt_after_cast` mode `until_tank`, tank 0 (shut above 70 % by the hp band); `ranged_only` 8 yd |
| Qiraji Gladiator | `linked_death` on itself, hold 5 %, window 7 s |

## Hakkar the Soulflayer (map 309, entry 14834)

Needs `DungeonRunPullGate`, `DungeonRunSeekAura`, `DungeonRunCharmYield` and `DungeonRunBossSpread` (all default `0`), plus aura tactics for the taunt row. The five High Priests must be dead first, one stack of Hakkar Power per living priest; the run leader never pulls while he wears it and ends the run after 30 s.
Before each Blood Siphon (every 90 s) the raid takes a Poisonous Blood dose from a Son held and killed at the fixed spot: DPS hold it at 30 % until the raid gathers within 4 yd, then burn it so the cloud lands on them. Dispel never touches the poison; Corrupted Blood keeps the ranged raid 12 yd apart. Cause Insanity cannot be taunted off, so the `threat_successor` row makes the damage dealers keep below the off-tank: on the reset the off-tank takes the boss instead of a damage dealer. An off-tank carrying only a tiny amount of threat (under 100, and under 1 % of the highest tank's) is not used, so the fight still deals damage while a charmed or just-reset off-tank is picked up.
His script evades him (full health) as soon as he steps below z 45.8, and that line runs 45-50 yd SW of his spawn, where the platform slopes away. The `anchor` row keeps every boss and raid move within 30 yd of his spawn (25 yd for the tanks) while he fights, and the seek spot stands 15 yd S of his spawn on the flat top (-11787.4, -1664.3, 54.0); the seek pull leaves a Son below the platform or past the anchor alone. A fight that ends with him alive and resetting is recorded with the outcome `evade`. The Sons patrol about 78 yd out, past both the tanks' reach and the shot from the anchor's edge, so when no Son is in reach one ranged damage dealer walks out: a hunter first, then a mage, warlock or shadow priest, then any ranged damage dealer, never a tank, a healer, his current victim, one at 90 % of his main tank's threat or more, a charmed bot, a bot outside the anchor or without a navmesh path to the stand, or one that cannot cast its shot at the Son right now (Auto Shot 75 for a hunter, the wand's Shoot 5019 for a mage, warlock or priest, Wrath for a druid, Lightning Bolt for a shaman). While that task runs (at most 25 s) it alone is free of the anchor and its pet stays passive at its heels (the pet's stance and stay/follow command come back on the bot's next update after the task ends, however it ends): it walks to a stand on the platform: the first point out from the anchor's edge toward a Son out of combat (the nearest one that has such a point), in 2 yd steps, with its floor at z 46.5 or higher (above his 45.8 evade line), within 28 yd of the Son and in sight of it (no such point: no walk-out, `seek pull: walk-out no stand`); there it casts the shot once (a cast that fails there aborts and spends nothing) and walks straight back to 20 yd from his spawn, inside the tanks' circle, where the off-tank takes the Son and drags it to the seek spot. The task aborts the moment the puller reaches 90 % of his main tank's threat, becomes his victim or is charmed, or after two refused moves in a row (that abort spends neither the Son nor the cycle, and the puller sits out the picks for 60 s); every abort logs `seek pull: walk-out abort <reason> dist=<yd> stand=<x,y,z> moved=<0|1>`; after a timeout or an abort the puller walks back exempt for at most 15 s, attacking nothing. One walk-out per Son, one at a time, one per Blood Siphon cycle; bot_events logs `seek pull: ranged walk-out <bot> -> <son guid>` and `seek pull: walk-out back|timeout|abort`.
The pull holds all damage for 5 s (`dps_hold_after`, no spell, mode `engage`) so the main tank takes Hakkar first, before Cause Insanity (every 20-25 s on his current victim) can charm a damage dealer. A re-pull re-arms it: the hold state resets when the boss is read out of combat or when nobody read it for more than 10 s (a wipe leaves the live attacker scan, so a quick re-pull is seen only as that silence).

## Known limits

Recurring threat-reset holds need a cast trigger. A `dps_hold_after` row with `spell_id` 0 fires only its engage edge, so it serves the opening 5 s Hakkar pull hold only and never comes back mid-fight (the spell-less Mandokir row was removed). Hakkar's Cause Insanity is answered by the `threat_successor` row's cap at the assigned off-tanks, which is what keeps the boss off the damage dealers; his `taunt_after_cast` row in mode `until_tank` stays inert because the game makes him immune to taunts (see the Hakkar section above). By contrast, a spell-less `taunt_after_cast` mode `until_tank` covers a scripted threat reset: Gurubashi Knock Away and Intimidating Roar leave the Berserker on a non-tank victim, which the episode watches for directly, so no cast slot is needed (see the Zul'Gurub table above). An instant teleport leaves no cast to read either, so a keyed `until_tank` row (`taunt_after_cast` with a `spell_id`) opens on its tank victim's displacement instead. Jin'do's Banish 24466 row did that until 20261106120000 deleted it: like Hakkar he is immune to taunts, so no taunt row can take him back; his `threat_successor` row and the `tank_ignore` troll row do that work instead.

## Stand sets

Own table `ai_boss_stand`, startup-only (restart needed). The strategy arms on a map with a row in either table.

| Column | Meaning |
| :--- | :--- |
| `map` / `boss_entry` | Where and whose fight. |
| `anchor` | Fight spot plus `x y r`: the boss at it within `r`. |
| `role` | `ranged`, `healer`, or `melee`. Tanks count as melee; healers seat first. |
| `window_spell` | `0` means always. |
| `aura` | Aura gating the stand. |
| `side` + `seq` / `x y z` | Seat position. `z` is put back on the ground. |
| `dx dy` | Slots 1-8 (default 5): a line of seats 9 yd apart. |
| `window_signal` | `cast` or `stopped_no_target`: still, targetless, with aura. `stopped_no_target` needs `aura` plus `window_spell`. |
| `hold_ms` | Default 8000, range 1000-15000, or until `release_anchor`. |
| `release_anchor` | Anchor that ends the stand. |

Primary key is `(map, boss_entry, anchor, role, window_spell, side, seq)`. Unusable rows are skipped with a log; a bad `release_anchor` for a boss drops that boss's set.

Seats go to same-role bots in id order. Taking part means being on the threat list or victim within 80 yd of the anchor, or holding a stand. Sides come from the lane cross product. While seated, the stand action walks the bot to its seat. After 3 refusals the seat is given up and reach or dodge takes over; seated bots suppress reach-spell, heal walk, dragon flank, and raid spread.

## Adding a boss

- Read the boss from the core: creature template, spells, events, scripts, spell template. Use Turtle ids only.
- Add rows in a NEW migration `data/sql/world/YYYYMMDDHHMMSS_world.sql` (idempotent; never edit an applied one).
- Run the SQL tests with the core checkout set: `tools/test_boss_tactics_sql.py`, `tools/test_boss_stand_sql.py`.
- Check in game with `DungeonTactics = 1` or a commander binding, then a boss pull.

## Not yet verified live

Uldaman, Deadmines, Wailing Caverns, Frostmane Hollow, Windhorn Canyon, Gilneas City, Karazhan Crypt, Crescent Grove, Zul'Farrak, Scholomance, Zul'Gurub, AQ20, Hakkar: none of the rows above are verified in game yet.