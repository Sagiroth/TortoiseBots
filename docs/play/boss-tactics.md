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
| `kind` | See Kinds below: `interrupt`, `kill_first`, `behind`, `avoid_cast`, `tank_hold`, `hold_kill`, `one_at_a_time`, `linked_death`, `seek_aura`, `seek_pull`, `pull_gate`, `spread`, `use_object`, `threat_successor`, `anchor`, `tank_ignore`, plus the aura kinds behind their own key (`dps_hold_after`, `dispel_priority`, `flee_on_aura`, `freeze_on_aura`, `taunt_after_cast`, `swap_on_aura`). |
| `target_entry` | The creature the row is about; `0` means the boss itself. `hold_kill` is inverted: `boss_entry` is the spared entry and `target_entry` is the blocker. `one_at_a_time` uses target `0` and `boss_entry` is the paced entry. `linked_death` lists one partner per row and `boss_entry` is the anchor. A `seek_aura` row names the source creature whose death leaves the cloud; a `seek_pull` row names that same seek source; a `pull_gate` row has `target_entry` 0 and names the boss itself, as does a `spread` row. |
| `spell_id` | `interrupt` / `avoid_cast` spell. `seek_aura`: the aura a bot must carry; `pull_gate`: the aura the boss must not wear; `spread`: the splash the spacing answers (documents the row only). |
| `priority` | `kill_first`: lower dies first. `hold_kill`: budget, how many blockers may live before DPS stop (`0` = not one more). `linked_death`: group id. `seek_aura`: the health % at which DPS stop on the source while the gather is not ready; `pull_gate`: most stacks allowed (`0` = none); `taunt_after_cast` with mode `until_tank`: which raid tank takes the subject. `anchor`: the tanks' radius in yards (`0`: the shared `param`). `seek_pull`: the most walk-outs started in one seek cycle (1-5). |
| `param` | `behind`: yards of front kept clear. `avoid_cast`: yards left. `linked_death`: hold percent, 0-100 exclusive of 0. `dps_hold_after`: hold seconds, 0-30. `seek_aura`: gather radius around the source; `pull_gate`: seconds the run leader holds before ending the run (1-300); `spread`: yards the ranged raid keeps apart (5-30); `taunt_after_cast`: window seconds (0 means 10). `anchor`: the circle's radius in yards for everyone, 10-60. `seek_pull`: the kill lead in seconds, 10-85 (the forced burn of an engaged source starts this long before the cycle spell is due). |
| `tactic_mode` | Only some kinds read it. `until_tank`: a `taunt_after_cast` row assigns a tank by raid order while the subject stays off a tank, and opens a new window every `param` seconds until a tank has it. `all`: an `avoid_cast` row clears the tanks too while the listed spell is cast (needs `AiPlayerbot.DungeonRunAvoidCastAll = 1`), instead of letting a tank the caster is on hold it. |
| `window_s` | `linked_death`: burn window seconds (default 7). `seek_aura`: dose window opens this many seconds before the cycle is due. `seek_pull`: the last start in seconds, 10-85 (no walk-out starts with less left to the due time). `0` for every other kind. |
| `cycle_spell`, `cycle_s` | `seek_aura` only: boss spell whose aura restarts the cycle clock, and the cycle length in seconds. `0` for every other kind. |
| `spot_x`, `spot_y`, `spot_z` | `seek_aura`: where the source is held and killed. `anchor`: the circle's centre (the boss's home). `0` for every other kind. |
| `burn_order` | `linked_death` only: kill order once released. First non-empty wins. |
| `hp_min` / `hp_max` / `bot_aura` / `mode` | Row gates, where read (never on `seek_aura`, `pull_gate`, `spread` or `anchor`). |
| `comment` | Free text. |

The true primary key is `(map, boss_entry, kind, target_entry, spell_id, hp_min, hp_max)`.

The loader skips unusable rows and logs the count: unknown kind, interrupt without spell, behind without radius, avoid without spell or radius, tank_hold with target_entry, hold_kill without target_entry, one_at_a_time with target_entry, linked_death without target_entry or param outside (0,100], `seek_aura` without target_entry, spell_id, cycle_spell or spot, param outside (0,10], priority above 99, cycle_s outside (0,600] or window_s outside (0,cycle_s), `pull_gate` with target_entry, without spell_id or param outside (0,300], `spread` with target_entry or param outside 5-30, `threat_successor` with target_entry or a gate, `anchor` with target_entry, spell_id or a gate, without a spot, param outside 10-60 or priority above param, `seek_pull` without target_entry, with a spell, a spot, cycle columns, a gate or a mode word, priority outside 1-5 or param/window_s outside 10-85, a cycle/spot/mode column the kind does not read, duplicate key.

### Kinds

| Kind | What bots do | Who |
| :--- | :--- | :--- |
| `interrupt` | A bot with a ready interrupt in range uses it: Pummel, Shield Bash, Kick, Counterspell, Earth Shock, first castable wins. Class interrupts on the current target are unchanged. Generic slot then channel slot, wider radius wins. | Any bot with an interrupt |
| `kill_first` | Takes the listed creature before anything else. Lowest priority first, then a creature whose victim is a healer of ours (a Shade of Jin'do on a healer comes before one on a damage dealer), nearer breaks the rest of the tie, nothing past 40 yd. Generic assist and AoE do not pull back. An explicit attack order wins, with one exception for the run's own orders: a bot carrying a row's `bot_aura` (Delusions 24306) turns on the nearest live unit of that row within 40 yd (the same reach for melee and ranged) even under the run's focus or attack order, which it drops; the run's focus skips it while that unit lives within 40 yd, never sends it at a totem, and once the aura is gone it returns to normal targeting. A bot that does not switch while its subject is in reach logs `BossTacticsKillFirst` with the reason and its current target, once per 5 s. A needs-aura subject takes direct damage only, so no DoT is cast at it and no pet is sent at it. A player's attack or focus order is never overridden. | DPS only, never tanks or healers |
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
| `seek_aura` | The raid doses itself from a dying source before the boss's cycle spell lands. The first siphon is timed from the fight's combat start, so cycle 1 gets the same dose window as every later one. While the dose window is open (`window_s` seconds before due, and up to 8 seconds past it when a sighting is missed, after which the cycle estimate rolls forward one cycle at a time (the grace stays under the gap between a cycle and its window, so the rolled cycle's window opens soon after)), bots lacking `spell_id` walk to the living source and stand within `param` yd; DPS bring it to `priority` % and stop until the gather is ready, then burn it so the cloud lands on the raid. A Son that is not fighting yet is fetched for real: the second off-tank walks to it (moving itself, not only setting a target) and hits it until it joins the fight, then drags it to the spot, and it keeps after that Son while the Son stays within 90 yd of the spot and for up to 20 seconds even after it leaves the 60 yd pickup radius; with fewer than three tanks alive (exactly two) the raid's nearest hunter (else a ranged damage dealer) opens a Son within 35 yd of the spot with one shot from its ranged weapon (the auto shot or a wand, never a damage over time whose ticks would run through the hold) and the first off-tank takes it over; a dead, charmed or boss-targeted member is never that puller. The source search reaches the spot's radius while three tanks are there, and 35 yd once the burn is forced, a source inside that reach already fights at or below the hold percent, or any source inside it is already fighting: that source becomes the gather and the kill pick wherever it stands. The only held hit that is let through is the puller's own strike on the Son its pull fetched, from the pull's start until that Son is dragged in (a DoT is still vetoed there). A Son that only joins the fight with the siphon due within 30 seconds is burned at once instead of waiting for the forced burn. A Son that fights a healer or another non-tank for 3 seconds is taunted off by the first off-tank when the third off-tank is dead or already holds a Son of its own. Only the picked source may be damaged, and only inside the window — every other Son stays held, and with the window shut the pick is left at whatever health it has so it serves the next siphon. An area cast that reaches a held Son is vetoed even when the picked Son beside it may burn, a Son that is not fighting is never picked, and once the whole raid carries the aura nothing is killed until the next siphon strips it. A damage-over-time with no direct part (Corruption, Serpent Sting) and a channelled one (Mind Flay, Drain Life) never goes on a Son, since its ticks would run through the hold; a nuke that also ticks (Fireball, Immolate, Moonfire) is allowed only on the Son the hold lets burn. The off-tank holding the Son stops auto-attacking it at or below the hold percent plus 10 while the window is shut, and keeps the threat. Hunters keep the siphon cycle: the pet is dismissed from 5 s before the due time until 2 s after it (and while the siphon's aura is on the bot) and called back afterwards, because an undosed pet is a full heal of its own. A warlock dismisses its demon once when Hakkar is engaged and summons nothing during the fight — a new demon costs a soul shard and a 10 s cast — and never an Infernal or a Doomguard. A shaman unsummons its totems for the same window and places none, since each totem is one more undosed unit. Needs `AiPlayerbot.DungeonRunSeekAura = 1` (default `0`); main tank and first off-tank never seek. | every bot without the aura except the two tanks; second off-tank pulls and holds the source (a hunter pulls when it is not there); pet owners send the pet away |
| `seek_pull` | The seek walk-out's per-cycle rules for one `seek_aura` source (walk 51). With `AiPlayerbot.DungeonRunSeekPullCycle = 1` and a row for the fight: each Son is walked again in every cycle (the walked list is per cycle), up to `priority` walk-outs (1-5) start a cycle while the siphon is at least `window_s` seconds away, a failed walk-out reopens the cycle (a puller-side abort frees its Son and leaves the puller out of the picks for 60 s; a no-stand abort, a vanished Son and a timeout keep the Son for the cycle), the stand search fans 10 and 20 degrees either side and falls back to a hunter's 33 yd (hunter only) when nothing fits at 28, a puller that stands at its stand 6 s without landing the shot aborts `no shot`, the pick takes any ranged damage dealer on the platform (60 yd of the anchor's centre, at or above z 46.5) even mid-cast or on a cooldown, the puller faces the Son every tick while in shot position, the no-shot clock keeps running through stand moves when the puller is already in shot position, no walk-out starts before the fight is 19 s old (the first Cause Insanity), and starts go from the seek_aura row's window (80 s for Hakkar) down to the 60 s last start; an engaged Son is burned from `param` seconds before the siphon. Hakkar's row uses a 75 s lead: a Son killed any time from 11 s after one siphon still doses the next (Poisonous Blood lasts 90 s and the siphon strips it only at its own 8 s aura's end), so the burn starts as soon as the pulled Son is engaged. The forced burn also skips the 4 yd gather, so the cloud lands where the receiving off-tank takes the Son, inside the tanks' circle; the walk measures `raid_in_cloud` as its guardrail. Documented limits: starts are allowed only from the seek row's window down to 60 s before the siphon, so the retries mostly serve fast threat aborts and a no-shot or no-stand abort rarely leaves time for a second walk-out; a missed siphon sighting rolls the due time but not the cycle number, so the walked list is not cleared until the next sighting; from 45 s the forced burn also marks the gather ready, so an engaged picked Son dies without waiting for the 4 yd gather; a stand that keeps jumping more than 3 yd while the puller is not in shot position can keep resetting the no-shot clock, which the 25 s timeout then catches; the `!s.shot` guard also covers timeouts (untested); the fight has one walk-out slot and a failed puller's walk back holds it, so a failed walk-out rarely leaves time for a retry (12 of walk 52's 148 starts were retries); the cycle-1 gate is a fixed 19 s, not a sighting of the first charm (the charm landed at +17-18 s in 55 of 59 fights, at +38-40 s in the other 3). `target_entry` the seek source, `spell_id` 0, no spot, cycle columns, gate or mode word. Read straight from the table by the walk-out. Needs `AiPlayerbot.DungeonRunSeekAura` and `AiPlayerbot.DungeonRunSeekPullCycle`. | Ranged walk-out pullers |
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
| Hakkar the Soulflayer | `dps_hold_after` 5 s from the pull (no spell, mode `engage`); `seek_aura` Poisonous Blood 24321 on a Son of Hakkar, hold at 30 %, gather 4 yd at (-11787.4, -1664.3, 54.0); `pull_gate` on Hakkar Power 24692, 0 allowed, hold 30 s; `spread` 12 yd on Corrupted Blood 24328; `threat_successor` (the damage dealers' threat cap is the assigned off-tanks' highest); `anchor` 30 yd around his spawn, tanks 25 yd; `seek_pull` 3 walk-outs a Blood Siphon cycle, none with less than 60 s left, a 75 s kill lead. Needs `DungeonRunSeekAura`, `DungeonRunSeekPullCycle`, `DungeonRunSeekSonMeet` (the pulled Son's meet) and `DungeonRunPullGate`, `DungeonRunBossSpread` (all default `0`); `DungeonRunSeekTakeEdge` (default `0`) lets the off-tank take a Son at the raid's edge. No `taunt_after_cast` row: like Jin'do he is immune to taunts, so 20261107200000 deleted the Cause Insanity cover and the `threat_successor` row does that work instead. |
| Jin'do the Hexxer | `needs_aura` Shade of Jin'do 14986 on Delusions 24306 (only a carrier targets it; its carrier turns on it under the run's focus, and healers weight up the member a Shade attacks); `kill_first` Shade (gated on 24306), Powerful Healing Ward 14987, Brain Wash Totem 15112; `dispel_priority` Delusions `never`; `tank_ignore` Sacrificed Troll 14826 (the damage dealers kill the trolls, the tanks stay on Jin'do); `threat_successor` (he is taunt-immune, so the damage dealers stay below the off-tank). No `taunt_after_cast` rows: his taunt immunity made both (Hex/charm and Banish 24466) useless, and 20261106120000 deleted them. Needs `DungeonRunAuraTactics` and `DungeonRunAuraGatedTargets`, plus `DungeonRunCarrierScan`, `DungeonRunTauntGate` and `DungeonRunCarrierHold` (all default `0`) for the rules below. |
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

Needs `DungeonRunPullGate`, `DungeonRunSeekAura`, `DungeonRunSeekPullCycle`, `DungeonRunSeekSonMeet` (the pulled Son's meet) and `DungeonRunBossSpread` (all default `0`). The five High Priests must be dead first, one stack of Hakkar Power per living priest; the run leader never pulls while he wears it and ends the run after 30 s.
Before each Blood Siphon (every 90 s) the raid takes a Poisonous Blood dose from a Son held and killed at the fixed spot: DPS hold it at 30 % until the raid gathers within 4 yd, then burn it so the cloud lands on them. Dispel never touches the poison; Corrupted Blood keeps the ranged raid 12 yd apart. A Son that has fought a non-tank for 3 s is the first off-tank's take; Taunt and Growl reach only melee, and the bots a Son hits stand on the raid circle's edge so the Son stands outside it: with `DungeonRunSeekTakeEdge` a tank still takes a Son outside the circle when its victim stands inside the raid circle (plus 2 yd), the Son's z is 46.5 or higher, and the Son is within 8 yd of the circle and within 10 yd of that victim (`reason=edge_far` when only those distances fail). The take row says why it did or did not taunt (`reason=chase_refused|no_taunt|attack|taunt`), and with `DungeonRunSeekSonMeet` the pulled Son meets a waiting off-tank: the puller's walk back follows that tank's live position (the tank keeps its normal tank AI), and the Son is taunted on arrival (the walk back ends within 8 yd of the tank or at the old circle-edge point, whichever comes first). While the meet is open and no tank has the pulled Son, the damage dealers hold it: it is never released by the 5 s non-tank clock or the forced burn, and its puller does not count as a non-tank for the healer take. The tank waits only while the Son's victim is the puller, the returner or nobody; a Son peeled onto anyone else closes the meet (`peeled`) and the normal take applies. 2 s later the hold lets the damage dealers kill it, the cycle's cloud lost (`seek take: <tank> -> <son> victim=<name> reason=<word> meet=<0|1>` in bot_events). A Son the hold has kept fighting for 20 s is released to the damage dealers whatever the window, the gather or its health say, so a single Son cannot stall the raid; only fighting time counts, so an idle Son waiting at the spot is held for as long as it waits (it is the reservoir the area veto protects). Cause Insanity cannot be taunted off, so the `threat_successor` row makes the damage dealers keep below the off-tank: on the reset the off-tank takes the boss instead of a damage dealer. An off-tank carrying only a tiny amount of threat (under 100, and under 1 % of the highest tank's) is not used, so the fight still deals damage while a charmed or just-reset off-tank is picked up.

Each cycle's Son story is readable in `bot_events.csv`: `BossTacticsSonHold` logs a Son entering the hold and being released (with the reason) or leaving it, `BossTacticsSonDeath` logs a Son's death with the raid members around its cloud, `BossTacticsHoldKill` carries the held Son's `held_ms=`, and `BossTacticsSiphon` carries `hp_seen=`, Hakkar's health at that cycle's first sighting. With `DungeonRunSeekSonMeet`, `BossTacticsSonMeet` logs each pulled Son's meet: the receiver it went to (`event=meet`) or why no tank could wait (`event=no_meet,reason=dead:1|busy:1`), the tank that took it (`event=taken`) and the meet's end (`event=lost,reason=son_dead|receiver_dead|receiver_charmed|receiver_victim|receiver_busy|peeled|timeout`; `event=wait,reason=receiver_wait` while the receiver waits). `BossTacticsCharm` measures Cause Insanity's successor: 1.5 s after a charm is first seen it logs the charmed player's role and class, Hakkar's new victim, the second and third tanks' threat on him and the five highest threat entries, so the next threat brief can read how far a damage dealer outranks the off-tank.

Known limits of these rows: the release row is the first bot whose read lets the Son go, and other bots may still hold it; an end row's `held_ms` runs to the hold clock's sweep (+5 s without a tick, +20 s unseen), while a dead Son's `held_ms` on the death row is the true hold time; `raid_in_cloud` is a 2D 8 yd count at the first read after the death, not over the cloud's lifetime; a corpse that leaves the map before any read sees it dead is logged `gone`; the raid members counted are filtered by map id only.
His script evades him (full health) as soon as he steps below z 45.8, and that line runs 45-50 yd SW of his spawn, where the platform slopes away. The `anchor` row keeps every boss and raid move within 30 yd of his spawn (25 yd for the tanks) while he fights, and the seek spot stands 15 yd S of his spawn on the flat top (-11787.4, -1664.3, 54.0); the seek pull leaves a Son below the platform or past the anchor alone. A fight that ends with him alive and resetting is recorded with the outcome `evade`. The Sons patrol about 78 yd out, past both the tanks' reach and the shot from the anchor's edge, so when no Son is in reach one ranged damage dealer walks out: a hunter first, then a mage, warlock or shadow priest, then any ranged damage dealer, never a tank, a healer, his current victim, one at 70 % of his main tank's threat or more, a charmed bot or one under Cause Insanity, a bot off the platform (more than 60 yd from his spawn, or below z 46.5) or without a navmesh path to the stand, or one without a shot at all (Auto Shot 75 for a hunter, the wand's Shoot 5019 for a mage, warlock or priest, Wrath for a druid, Lightning Bolt for a shaman); a caster already mid-cast or with its shot on cooldown is picked, and the walk-out interrupts that cast on its way to the stand. While that task runs (at most 25 s) it alone is free of the anchor and its pet stays passive at its heels (the pet's stance and stay/follow command come back on the bot's next update after the task ends, however it ends): it walks to a stand on the platform: the first point out from the anchor's edge toward a Son out of combat (the nearest one that has such a point), in 2 yd steps, with its floor at z 46.5 or higher (above his 45.8 evade line), within 28 yd of the Son and in sight of it (no such point: no walk-out, `seek pull: walk-out no stand`); there it faces the Son every tick while in range and sight of it, casts the shot once (a cast that fails there aborts, frees the Son and leaves the puller out of the picks for 60 s) and walks straight back to 20 yd from his spawn, inside the tanks' circle, where the off-tank takes the Son and drags it to the seek spot; with `DungeonRunSeekSonMeet` the walk ends at the waiting off-tank's own position instead (clamped inside the circle), and that tank takes the Son as soon as it is within 8 yd, so its melee taunt reaches. The stand is re-planned every second from the Son's live position (the Sons patrol), with the same rules and the puller's own reach and path; a move of more than 3 yd restarts the 6 s no-shot clock, except while the puller already stands in shot position (range, floor and sight line), where the clock keeps running so a shot that never lands still aborts; 5 s straight without any stand aborts (`no stand`). A cast or auto-repeat is stopped before the walk. The task aborts the moment the puller becomes his victim or is charmed, the moment its threat on him reaches the live top tank's (the highest living tank on his threat list that tick, reason `threat`; the pick itself still needs less than 0.7 of the main tank's), or after three refused moves in a row (that abort frees the Son for the next cycle and leaves the puller out of the picks for 60 s); every abort logs `seek pull: walk-out abort <reason> dist=<yd> stand=<x,y,z> moved=<0|1> path=<type> len=<yd> why=<mover reason>`; after a timeout or an abort the puller walks back exempt for at most 15 s, attacking nothing. A walk-out starts once the next Blood Siphon is due in the seek row's window or less (80 s with Hakkar's `seek_aura` row, the whole dose window) and the fight is at least 19 s old (the first Cause Insanity lands at +17-18 s; a pick before it aborted on the top tank's threat). With the anchor row a Son inside the anchor's 30 yd is in the seek's reach wherever it stands there (the reach is otherwise 35 yd from the seek spot), so a Son brought to the anchor's far edge is burned. One walk-out at a time. With `AiPlayerbot.DungeonRunSeekPullCycle` off (default) the old rule holds: never twice to a Son still alive (a Son that died may be walked to again once it respawns), and one per Blood Siphon cycle (a walk-out that times out, finds no stand or fails its cast or moves is not retried in the same cycle). With the key on and a `seek_pull` row for the fight, the walked list is per cycle, so each Son may be walked again in every cycle; up to the row's three walk-outs start a cycle, each only while the siphon is at least the row's 60 s away at the start, a failed walk-out reopens the cycle (a puller-side abort, such as a threat abort, also frees its Son and leaves that puller out of the picks for 60 s, so the retry takes another bot; a no-stand abort, a Son that vanished and a timeout keep the Son walked for the cycle, so the retry takes the other one), the stand search fans 10 and 20 degrees either side of the Son's bearing and, when no stand fits at 28 yd, searches again at a hunter's 33 yd and picks a hunter only, and a puller that has stood at its stand 6 s without landing the shot aborts unspent (`no shot`). An engaged Son is burned from the row's 45 s before the siphon instead of the 30 s late-engaged lead. At the stand the puller waits only for a cast of its own aimed at the Son; its Auto Shot or wand's Shoot still on Hakkar is interrupted, so the shot goes out at once. bot_events logs `seek pull: ranged walk-out <bot> -> <son guid>` (with a `seek_pull` row the same row also carries `due=<s> try=<n> reach=<yd> ray=<deg> tr=<bot threat / live top tank's>`), `seek pull: walk-out back|abort`, `seek pull: walk-out late due=<s> need=<s>`, `seek pull: walk-out abort no shot dist=<yd> ... los=<0|1> inrange=<0|1> spell=<id> onson=<0|1> front=<0|1> moving=<0|1> dz=<yd>`, `seek pull: walk-out no son walked=<n>`, `seek pull: walk-out no stand sons=<n> reach=<yd> hunter_pass=<0|1>`, `seek pull: walk-out no puller stand=<x>,<y> reach=<yd>` (its info2 carries `reason=<code>:<n>|...,members=<n> top=<live top tank's threat> low=<lowest candidate's threat>`, the codes in the pick's own precedence order: `away`, `no_ai`, `hunter_only`, `dead`, `tank`, `healer`, `no_role`, `victim`, `threat`, `cc`, `no_shot`, `outside`, `bench`, `no_path`) (the no-son, no-puller, no-stand and late rows are throttled to 10 s each) and `seek pull: walk-out timeout shot=<0|1> dist=<yd> retry=<0|1> spell=<id> los=<0|1> z=<z> stand=<yd>`. The abort that a re-pull or an evade closes adds ` cause=<repull|evade>` after its `why=` field. The full inventory of refusal rows and their word sets is [`refusal-rows.md`](refusal-rows.md).
The pull holds all damage for 5 s (`dps_hold_after`, no spell, mode `engage`) so the main tank takes Hakkar first, before Cause Insanity (every 20-25 s on his current victim) can charm a damage dealer. A re-pull re-arms it: the hold state resets when the boss is read out of combat or when nobody read it for more than 10 s (a wipe leaves the live attacker scan, so a quick re-pull is seen only as that silence).

## Jin'do the Hexxer (map 309, entry 11380)

See the Zul'Gurub table above for his rows. Delusions of Jin'do 24306 picks the carriers: only a carrier can hurt a Shade of Jin'do 14986, and only a carrier targets it.

### Carrier strike

Behind `AiPlayerbot.DungeonRunCarrierStrike` (default `0`): a cursed healer or off-tank turns on its own Shade while the raid can spare it, striking just below a critical heal so real healing always wins. Damage dealers never strike (the kill-first row already sends them at Shades), and the main tank never leaves the boss.

The strike is refused when the bot carries no Delusions, when no Shade stands within 40 yd, when an off-tank holds the boss, or on any fight whose boss has a `threat_successor` row: leaving a boss that cannot be taunted would drop the damage dealers under half the main tank's threat and hand him to one on the next threat wipe, so no off-tank strikes there. A healer also stays on the raid while a `now` dispel waits that it could cure, while the lowest member in heal reach is under the low-health line (`AiPlayerbot.LowHealth`, default 50 %), while the main tank is under 60 %, while fewer than 2 other healers that can heal the raid are standing, or while it is under 30 % mana; the lowest-member and main-tank lines hold an off-tank too. A healer on another floor does not count as cover: more than 6 yd of height difference (down in the troll pit, or up on the room floor while the carrier is in the pit) puts it out of the raid's healing, as do death, the curse itself, or a charm.

### Finding the Shade

Behind `AiPlayerbot.DungeonRunCarrierScan` (default `0`): the Shades are summoned on a carrier and only a carrier can see them, so a carrier's attackers list can miss the Shade standing beside it (walk 53: 65 % of Shade-frames had no cursed bot attacking the Shade, and 295 of 357 kill-first no-switch rows said only `not_read`). With the key on, a carrier (a free DPS or one under the run's own order) reads the Shade's entry straight off the nearby grid and takes the nearest one that passes every check, in this order: a living Shade of that entry is there (`no_unit` when none is), it belongs to this fight (`ineligible`), it stands within 40 yd (`beyond_reach`), the bot can see it (`no_los`), and the bot may attack it (`invalid_target`). The checks are the same rules the ordinary pick uses, so the scan never widens what a carrier may attack, and a player's own attack or focus order is never overridden (`player_order`). The scan runs before the ordinary pick is accepted: a free DPS takes every open `kill_first` row, so a Powerful Healing Ward or a Brain Wash Totem in view would otherwise win the pick while a Shade lives, and a carrier is the only one who can kill that Shade (the uncursed raid is left the ward and the totem). A Shade the scan finds therefore replaces a ward or totem pick; with no Shade found, or for a bot without the curse, the ordinary pick stands, and an ordinary pick that already is a Shade is kept as it is.

The `BossTacticsKillFirst` row then carries the word above, or `not_pickable` when a Shade is in the fight and not attackable at all, and `attack_refused` when the attack on any carrier pick was refused anyway (the scan's or the ordinary one); it also appends `codes=<word>:<n>|...` counts (`none:<n>` counts the hits that passed every check), `grid=<hits>`, `pickable=<0|1>` and `mode=<carrier|free|none>`, so a no-switch row no longer needs a frame walk to explain itself. When the scan's Shade replaces an ordinary pick, one `BossTacticsKillFirst` row per bot per 5 s says `reason=scan_over` with `pick=<entry>:<guid>,over=<entry of the replaced pick>,mode=<carrier|free>`. With the key off the pick and the rows are exactly as before.

With the key on, the grid-found Shades are also seen by the two other readers of the carrier pick: the carrier-strike pick (`FindBossCarrierStrikePick`, which a cursed healer or off-tank uses) and the Commander's focus skip (`Commander.cpp`, which leaves the run's focus alone while a carrier has a Shade to hit).

### The carrier hold

Behind `AiPlayerbot.DungeonRunCarrierHold` (default `0`): a Delusions carrier standing on its Shade keeps it. The "not dps target active" trigger fires whenever the bot's current target differs from its "dps target" (the skull, Jin'do), and its action "dps assist" (relevance 60) outranks kill-first (21), so a carrier was pulled off a live Shade for the boss (76 times in walk 54, 65 in walk 53). With the key on the trigger stays quiet when the current target is a needs_aura subject the bot carries the aura for (`CarrierHoldsCurrent`); a bot without the aura on the Shade, a carrier on any other target, and every bot with the key off keep today's rule. The kill-first trigger is also checked every second under the key, whatever `DungeonRunTotemFocus` says, so the re-pick after a Shade dies no longer waits up to two seconds (median 2.0 s before the key). Needs `DungeonRunAuraTactics` for the needs_aura rows.

CarrierScan is redundant while KillFirstGrid is on: the ordinary grid pass reads the same kill-first units, and the Shade's kill_first row has priority 0, which beats the ward and the totem (walk 54: every `no_unit` row was an honest "no Shade within 40 yd"). The scan stays in the code; run with `DungeonRunCarrierScan` off.

### The taunt gate

Behind `AiPlayerbot.DungeonRunTauntGate` (default `0`): a tank does not taunt a Shade of Jin'do (only a carrier may damage it, and a taunt only pulls its Shadow Shock onto a second tank) or a creature the core marks immune to taunts (Jin'do, where a taunt moves the tank's threat onto a damage dealer). Every single-target taunt is gated: the warrior's Taunt (the taunt row, the snare-target and off-target forms) and Mocking Blow, the druid's Growl, the paladin's Hand of Reckoning, the shaman's Earthshaker Slam (each with its on-attacker form), and the boss actions that cast a class taunt themselves (the tank swap, the seek take, the taunt-after window). A refusal logs `BossTacticsTauntGate` with `reason=needs_aura` (the bot lacks the row's aura), `reason=holding_boss` (a Delusions carrier that is the boss's current victim, the main tank holding Jin'do: a Shade taunt there moved the boss's threat off it, 19 of 114 Shade taunts in walk 54; a cursed off-tank that does not hold the boss may still taunt the Shade) or `reason=taunt_immune`, at most once per bot and reason per 5 s (a `taunt_immune` row no longer hides a `needs_aura` row in the same window). The gate also drops a needs_aura unit (the Shade) from the "attacker of non-tank target" scan, which feeds both the taunt actions and the warrior gap closer's Charge: an uncursed warrior cannot hurt a Shade either (a tank that carries Delusions keeps the Shade in that scan and may taunt it). The area taunts, Challenging Shout and Challenging Roar, have no single target and are not covered, a documented limit. The gate reads the subject's aura row and taunt immunity, so it refuses exactly the two subjects above (the Shade only at a bot without Delusions) and nothing else.

Limits of the gate: (a) none for a Delusions carrier any more: a tank that carries Delusions may taunt the Shade, since it can damage it, and keeps it as a target and as the warrior's Charge target; an uncursed tank is refused at the Shade and the "attacker of non-tank target" scan skips it, so the scan and the gate agree for both kinds of tank (the taunt-immunity check is unchanged and applies to a carrier too); (b) on a taunt-immune boss with a `taunt_after_cast` or tank-swap row (AQ20 Ossirian 15339 and Ayamiss 15369, `immunity_flags & 0x02`) the gated action also skips the off-tank's switch to the boss, because the old taunt never landed there anyway. Revisit when AQ runs resume.

The seek take's `LogSeekTake` row says `reason=taunt_gated` when the gate refuses the cast (see `docs/play/refusal-rows.md`).

## Known limits

Recurring threat-reset holds need a cast trigger. A `dps_hold_after` row with `spell_id` 0 fires only its engage edge, so it serves the opening 5 s Hakkar pull hold only and never comes back mid-fight (the spell-less Mandokir row was removed). Hakkar's Cause Insanity is answered by the `threat_successor` row's cap at the assigned off-tanks, which is what keeps the boss off the damage dealers; like Jin'do he is immune to taunts, so his `taunt_after_cast` row was deleted by 20261107200000 and his section above lists no taunt row. By contrast, a spell-less `taunt_after_cast` mode `until_tank` covers a scripted threat reset: Gurubashi Knock Away and Intimidating Roar leave the Berserker on a non-tank victim, which the episode watches for directly, so no cast slot is needed (see the Zul'Gurub table above). An instant teleport leaves no cast to read either, so a keyed `until_tank` row (`taunt_after_cast` with a `spell_id`) opens on its tank victim's displacement instead. Jin'do's Banish 24466 row did that until 20261106120000 deleted it: like Hakkar he is immune to taunts, so no taunt row can take him back; his `threat_successor` row and the `tank_ignore` troll row do that work instead.

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