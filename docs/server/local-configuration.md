---
id: local-configuration
title: Local Companion Configuration
category: server
summary: Configuration for retained local provisioning, consumables and diagnostics.
tags: [local, companions, raids]
relates_to: [play-commands, local-raid-tactics, local-companion-controls, local-configuration]
---

# Local Companion Configuration

These retained local controls supplement the current upstream guides. They do not enable a tactic unless its documented opt-in or private default says so. No new live validation is claimed by this rebase.

### Experimental hire validation

`TortoiseBots.ExperimentalHireSelfTest = 0` in the main `mangosd.conf` is the
startup-only default. Set to 1 only on an isolated experimental runtime to run
one disposable Fury hire through the production service, check the shared
inspection result for Fury/DPS/17 equipped locked slots, and dismiss its newly
created fixtures. It does not use existing characters. Disable after validation.
Automatic locked gearing of eligible new level-60 hires is part of experimental
provisioning and uses the unchanged six explicit profiles; unsupported specs are
reported and retain normal starter gear.


### Role consumables (issue #491)

`AiPlayerbot.RoleConsumables.Enabled = 0` is the feature default before the private provisioning override described below. When enabled,
only bots grouped with their live human master participate. A roaming pool bot
or bot-only group never follows this path. Battlegrounds retain their existing
consumable behavior. Restart after changing these configuration settings.

The real master's rewarded quests unlock stages in order. Set
`AiPlayerbot.RoleConsumables.PotionQuest`, `AiPlayerbot.RoleConsumables.ElixirQuest`
and `AiPlayerbot.RoleConsumables.FlaskQuest` to your server's quest IDs. All default
to `0` (locked); the elixir stage also unlocks scrolls and food buffs. Quest creation
and reward changes are not included. A later quest does not bypass an earlier stage.

Bots follow a supported active consumable buff on their master, including a buff
already present when they join. They choose their own role's item from their bags:
Titans for tanks/physical damage, Distilled Wisdom for healers, Supreme Power for
casters. Elixirs favor armor, intellect/spirit, spell damage or agility respectively;
scrolls favor stamina, intellect or agility. Lower-level/bag alternatives are used
when available. One active supported buff per category is retained, avoiding
repeated replacement. Food buffs follow the same rule and use existing bags only.

The bounded Classic catalogue lives in `runtime/RoleConsumablePolicy.h`. Custom
consumables and specialized resistance/utility effects are not automatically
selected. The native item and spell definitions supply level/class restrictions,
stack limits, cooldowns and cast validation. There is no per-tick database query or
item-template scan. In combat, health potions are considered below 30% and mana potions below 65%
when the existing critical/low-health and low/medium-mana triggers fire; health
takes precedence. Native restricted shapeshift forms, stealth and invisibility
are left intact; this automation does not shift or reveal a bot to use an item.
Normal item-use packets consume actual inventory even if item cheats are enabled.

Disable use with `nc -role consumables` and `co -role consumables`; re-enable with
the corresponding `+role consumables`. Automatic provisioning is a separate
server setting. This feature does not interrupt an ongoing cast. Older broad
consumable/potion actions are suppressed for managed companions to keep them from
bypassing the new quest and inventory gates; pool behavior is unchanged.

#### Optional private-server provisioning

The provisioning feature defaults to `AiPlayerbot.RoleConsumables.AutoProvision = 0` before the private override below.
Setting it to `1` also enables role-consumable behavior. Only ledger-backed hired
companions receive free supplies and bypass all three quest gates. Owned alts and
claimed pool bots still require the configured quests and their own items.

The same bounded top-up runs through companion restocking and on the first safe
update after hire/login/resurrection, then every five minutes. Combat, travel,
casting and trading defer it. The target stock is 2 flasks, 5 elixirs, 5 scrolls,
and 10 each of usable health/mana potions, capped by native stack limits. Counts
include bank items; no items are deleted, downgraded or evicted to make room. Full
bags skip a top-up. Food is not provisioned. Existing legacy companion supplies
remain governed by their original factory behavior.

The local BiS equipment service has no inventory-supply seam. This implementation
reuses `PlayerbotFactory::RestockCompanion` instead and shares one idempotent
provisioner with the lifecycle clock. No equipment profile or core hook is changed.

#### Private build override (patch 303)

Waterys419's private configuration patch sets AutoProvision's template and missing-key
fallback to `1`. An explicit `AiPlayerbot.RoleConsumables.AutoProvision = 0` in the
active configuration disables provisioning and its hire-only quest bypass.

### Low-volume healer evidence

`AiPlayerbot.HealerDiagnostics = 1` is enabled by default and also requires
`bot_events.csv` in `AiPlayerbot.AllowedLogFiles`. Set it to `0` to disable.
It adds no columns and makes no gameplay decisions. Grouped, living healers
can emit at most one `HealSkip` and one `HealState` per ten seconds each.

`HealSkip` records a rejected generic heal action's range/cast check, or
`execute-refused` when execution returns false without a specific result.
The core result number accompanies check failures. A failed check is not
a failed heal landing: candidates may be reconsidered or another spell chosen.

`HealState` samples a live group roster at most once per ten seconds, when
there is injury below AlmostFullHealth or observed heal activity. It includes
injured/nearby counts, the lowest-health nearby member, health, distance, LOS,
map/instance, mana, moving/casting/combat flags, motion-generator type and
current destination (when available), last known executed action,
and counts of rejected checks, cast attempts and accepted attempts since the
last snapshot. Nearby uses the configured AI heal range, not an individual
spell's cast range. Self is included, pets are not. The last action may predate
the snapshot. Accepted attempts do not prove healing landed.

Snapshots describe observations, not inferred causes: moving can coexist
with an instant heal, and low mana alone does not prove an unaffordable cast.
Only actual failed checks get reason labels. There is no database query,
world scan, pathfinding or extra spell feasibility probe. Special actions
that bypass the generic spell action are outside the attempt counters; full
action logging remains a separate opt-in diagnostic.

### Patchwerk opening delay

`AiPlayerbot.PatchwerkOpeningDelay = 4` gives an assigned `patchwerk main`
and its soakers an opening lead. Positive values are clamped to 3–5 seconds;
0 disables this encounter-specific hold. DPS wait during the explicit main
pull approach, then the shared timer starts when engagement is observed
(up to one second detection latency). The deadline itself is checked in
milliseconds. No threat threshold extends or renews it. Tanks, healers, other
bosses and groups without a `patchwerk main` assignment are unaffected.
Both shipped configuration templates and the code default use 4.
