---
id: developers-bot-mechanics
title: Deep Bot Mechanics, Quirks & Capabilities
category: developers
summary: Comprehensive technical deep-dive into target selection algorithms, threat distribution, movement, interrupts, resting, and internal quirks.
tags: [mechanics, quirks, targeting, threat, interrupts, movement, ai]
relates_to:
  - developers-strategy-engine
  - developers-architecture
---

# Deep Bot Mechanics, Quirks & Capabilities

This document provides a thorough technical breakdown of how TortoiseBots functions under the hood: how targeting decisions are calculated, how movement and threat interact with the core engine, subtle quirks, and internal implementation guards.

---

## 1. Targeting Algorithms & Threat Distribution

Targeting in TortoiseBots is governed by specialized value calculators rather than a flat "nearest mob" check:

| Role / Intent | Target Calculator | Selection Logic |
| :--- | :--- | :--- |
| **Tank** | `TankTargetValue` (`FindTankTargetSmartStrategy`) | **Bucketed Aggro Priority:** attackers are bucketed — loose mobs the tank holds nothing on first (nearest), then held mobs in melee reach, then held mobs out of reach; among held mobs the current target wins and lowest personal threat orders the rest. `TankAssistTrigger` only switches while the tank still holds its current target (`has aggro`) and never while that held mob is at or below `AiPlayerbot.LowHealth` (50%): the tank finishes a low mob before peeling a loose add, then can switch back to anything it left. Multi-tank split: the explicitly flagged main tank (`Group::GetMainTankGuid`) with an off-tank beside it holds its mob — current first, every other candidate after, so loose adds never pull it off. Every other tank peels: a mark another tank already holds drops out of its pick, the loose-add bucket hands it the add instead of doubling up on the skull, and an off-tank parked on the main tank's skull still counts as holding it for the assist gate. Priority 1 = Explicit `.bot action attack` target; then (Naxxramas only) the horseman at a Four Horsemen mark carrier's new corner while that rotation lasts; Priority 2 = Raid Target Icon; strictly ignores CC targets. |
| **DPS (Single Target)** | `DpsTargetValue` | **Tank-First, Then Lowest Health:** Priority 1 = Explicit `.bot action attack` target; then (Naxxramas only) the horseman at a Four Horsemen mark carrier's new corner while that rotation lasts; Priority 2 = Raid Target Icon (**Skull**); then runners under 30% health ([shared runner rules](../classes/overview.md#shared-bot-behaviors)); Priority 3 = the live group tank's current target (when not CC'd); Priority 4 = Non-CC attacker with the lowest current health. |
| **DPS (AoE)** | `DpsAoeTargetValue` | **Highest Health First:** Targets the enemy with the highest health so damage-over-time (DoT) effects and cleaves tick for the longest possible duration. |
| **Crowd Control** | `CcTargetValue` + `HasCcTargetTrigger` | **Exclusive mark ownership + gated CC:** one mark = one bot; `.bot action cc clear` resets marks; own-aura re-CC allowed. Inside non-raid dungeons CC fires only on assigned mark unless `auto cc` is active. Auto-CC targets loose adds attacking healers, skips targets with periodic damage, never targets the last mob, Skull, or tank's target (1 auto target per bot, no re-sheep). Smart exclusions: current target, mobs below `MediumHealth`, and active AoE zones. |
| **Grind Target (destination band)** | `GrindTravelDestination` / `GrindSpotPolicy` | **Level-appropriate band:** an autonomous bot only accepts a grind destination whose creature `level_max` sits in `[botLevel - 2, botLevel + 1]` below level 10 and `[botLevel - 2, botLevel + 2]` from level 10. Quest objective / quest-loot spawns keep the same cap: a creature past it is no destination, and a point whose 40 yd surroundings hold hostile spawns past the cap is skipped. A spot is skipped once as many bots hold it as it has room for. |
| **Grind Target (mob pick)** | `GrindTargetValue` + `PullRegenPolicy` | **Low-Level Order Cap + XP eligibility + masterless leash + pull-regen gate:** the mob picked by `attack anything` (solo grind only). A bot below level 10 with no real player master and outside a battleground takes targets at most 1 level above its own; from level 10 the ceiling is +4. A wounded masterless pool bot starts no new pull below [`AiPlayerbot.MediumHealth`](../server/settings/combat.md#aiplayerbotmediumhealth) or [`AiPlayerbot.MediumMana`](../server/settings/combat.md#aiplayerbotmediummana). Followers keep the free-move leash to the master. |
| **Attack Order (auto-attack)** | `AttackAction` | **Auto-attack arming:** `bot->Attack(target, meleeAttack)` arms `UNIT_STAT_MELEE_ATTACKING`. A bot designated ranged with no usable ranged weapon (e.g. pre-10 druid on Wrath, elemental shaman) is armed for melee so it swings once the mob closes. |
| **Dispel / Decurse (party)** | `PartyMemberToDispel` (`DispelTargetPolicy.h`) | **Spread, in reach, in sight:** a member carrying a curable aura is ranked by cast range, line of sight, and unpicked status. Each bot claims its pick for 4 s per dispel type so multiple decursers clear multiple targets simultaneously. |
| **Enemy Player (PvP)** | `EnemyPlayerValue` / `EnemyPlayersValue` | **Fighting it first, then in reach:** duel opponent, players attacking the bot, then proximity (LOS, 60 yd overworld / 40 yd battlegrounds, 20 yd if the enemy has more health). Flag carriers prioritized. |
| **Enemy Healer** | `EnemyHealerTargetValue` | Detects enemies casting interruptible heals in range and surfaces them as high-priority interrupt and focus targets. |


### Helping a party member in a fight

A grouped bot fights what attacks any living member of its group within `AiPlayerbot.SightDistance` (default 60 yd, 2D) of it, not only what attacks itself: `AttackersValue::AddTargetsOf(Group*)` adds each such member's attackers (the core's attacker set, its current and last targets, and its pet's attackers), and `AttackersValue::IsValid` keeps one only while it is fighting that member (threat on it or it is its victim) and is not evading, set aside by the aura keep-out, or given up as unreachable. The "attack" free-move range is 0 (unlimited), so a bot in `follow` (or `stay`/`guard`) does not refuse such a target for its distance from its leader; the DPS and tank target pickers then choose among these attackers as for any fight. This is the same rule as mod-playerbots (`AttackersValue::AddAttackersOf(Group*)`, members within SightDistance). Live check (Uldaman, 2026-10-17, `tests/uldaman-events-2`): a revived mage fought a scorpid 73-76 yd from the party, out of that reach, and the party stood; when the mage fled to 50 yd the two rogues at the tank attacked the scorpid at once. With the run leader (`.bot run`), a bot coming back with attackers from farther out does not stop to fight them: it runs on to the party as a passive follower and fights them with the party once within 12 yd ([dungeon run leader](../play/dungeon-run-leader.md), *regroup: \<bot\> running back with N attackers*); the party does not walk out to it.

---

## 2. Tactical Interrupt & CC Resolution Gimmicks

When `.bot action interrupt` or `.bot action cc <mark>` is issued, the server resolves executors, validates conditions, and handles reach-queuing:

```text
Player Issues .bot action interrupt
    │
    ▼
1. Target Validation: Is target alive, hostile, and actively casting a non-melee spell?
    │
    ▼
2. Executor Selection: probes the action graph for a ready interrupt:
   - Rogue: Kick
   - Warrior: Pummel (Berserker) / Shield Bash (Battle/Defensive + Shield)
   - Shaman: Earth Shock
   - Mage: Counterspell
   - Priest: Silence (Shadow)
   - Warlock: Spell Lock (Felhunter pet) / Death Coil
   - Paladin: Hammer of Justice (Stun interrupt) / Repentance
   - Druid: Bash (Bear)
    │
    ▼
3. Execution / Reach Prerequisite:
   - If in range: Casts immediately.
   - If out of range: Enqueues reach action so the bot closes distance.
```

### Pet Discipline Around Crowd Control
Pets belonging to Hunter and Warlock bots are kept off CC'd targets via the breakable/unbreakable CC checks (`HasBreakableCC`: *Polymorph*, *Frost Nova* / frozen state, *Sap*, *Gouge*, *Shackle Undead*; `HasUnBreakableCC`: stuns, fears, roots) unless the Skull mark opts out. The *Seduce* aura is in `HasBreakableCC` (as well as the AoE-interlock list), so seduced marks read as held and pets are kept off them.

---

## 3. Movement, Formations & Physics Interactions

Bot movement bridges native C++ AI directly to the core server's `MotionMaster`:

| Movement Subsystem | Implementation Details |
| :--- | :--- |
| **Formations** | Supports 10 formations: `default`/`near`, `melee`, `queue`, `chaos`, `circle`, `line`, `shield`, `arrow`, `far`, and `custom`. Offsets calculate relative to master orientation. Mounted bots follow 3 yd further out. |
| **Catch-Up Mount (`BoostFollow`)** | When enabled ([`AiPlayerbot.BoostFollow`](../server/settings/party.md#aiplayerbotboostfollow)), bots far behind the leader mount up to catch up rather than trailing on foot. Combat-safe. |
| **Hunter Dead-Zone Weaving** | When an enemy closes to melee, a hunter fights back with *Wing Clip* and *Mongoose Bite* / *Carve*. Below level 10, hunters step back out of dead zone via `enemy too close for auto shot` -> `flee`. |
| **Kiting & Fleeing** | Pool bots flee at critical health or when outnumbered by 2+ attackers. Controlled bots with a real master never flee autonomously. Flee moves toward the group anchor or takes a fanned retreat step (8-16 yd) avoiding water and unpulled hostiles. |
| **Failed Escape Headings** | Destination-based flee and combat spread keep two-entry failure caches scoped to anchor and map. Gaining 2 yd clears the cache; failing to gain distance over 3 s remembers the heading as failed for 5 s. |
| **Elevators & Transports** | Moving transports use the transport teleport type setting (default `2`) to safely synchronize coordinates or teleport between docks rather than desyncing on complex geometries. |
| **Party Tank-Face & Melee Rear** | Tanks holding mobs sidestep so the mob faces away from the party (`tank face needed` -> `tank face away`). Melee DPS sidestep out of frontal and tail cleaves to flank and rear positions (`rear flank` -> `set behind`). |

---

## 4. Sustenance, Resting & Gear Evaluation

| Mechanic | How It Works |
| :--- | :--- |
| **Simultaneous Eat & Drink** | Out of combat, bots eat and drink in a single rest phase. Cheat bots recover to 90% health and mana; non-cheat bots to 50% health and 85% mana. A rest order (`OrderRest`) skips those stop lines and runs the drink and food actions directly, up to 100%. A bot already seated with the same meal keeps it, and a cheat bot gets the missing meal added as an aura, because a fresh cast stands it up and ends the meal. The order sets the `rest to full` value. While it is set, every meal lasts the whole 30 s, the eat and drink stop lines are 100%, and the `resume drink` / `resume eat` triggers start another meal if one was not enough. Any seated bot stands as soon as every meal it has is done (full mana for a drink, full health for food). The value clears once both bars are full or a fight begins. Pre-pull meal checks outrank `attack anything` until above medium health/mana (70/40 default). |
| **No Meal While Group Fights** | Bots do not sit down to rest while group members within 60 yd are in combat. Eating or drinking bots immediately stand up when combat begins. |
| **Level-Tier Rations** | Pool bots are seeded with level-appropriate food and water that upgrade upon leveling and restock automatically. |
| **Conjured Item Sharing** | Mages out of combat conjure food and water and trade stacks to mana-using allies in need. |
| **Gear Upgrades & Scoring** | When [`AiPlayerbot.RandomGearUpgradeEnabled = 1`](../server/settings/world-population.md#aiplayerbotrandomgearupgradeenabled), bots evaluate gear using spec-weighted stat formulas from `ai_playerbot_weightscales`, equipping upgrades and replacing incompatible weapon types. |
| **Bag & Quiver Management** | The item engine allocates 3x 14-slot Journeyman's bags, prioritizes quivers/ammo pouches for hunters, and manages soul bags for warlocks without displacing main bags. |
| **Initial Skill Seeding** | On persistent-level servers, fresh bots receive capped weapon skills, First Aid, and complementary gathering/crafting professions with appropriate tools. |
| **Vendor Trip Rules** | Bots liquidate stock when funds cover the cheapest green rank or at ≥8 items at 60% bags (suppressed in instances or with a real master; liquidates entire stock). |
| **Protected Inventory** | Food, drink, bandages, potions, reagents, ammo, and quest items are never sold or destroyed by cleanup routines. |
| **Full-Bag Cleanup Order** | At 90% capacity, cleanup destroys useless items first, then excess quest items, then cheap grey items, then profession materials, preserving consumables. |

---

## 5. Technical Quirks, Gaps & Internal Implementation Guards

For player-facing quirks and troubleshooting, see [Known Limitations & Quirks](../play/known-limitations.md).

| Subsystem / Quirk | Technical Implementation & Guard Logic |
| :--- | :--- |
| **Upkeep Buff Mana Floor** | Out of combat upkeep buffs require post-cast mana at or above 40% (70% charged); companions with a real player master use 20% (40% charged). Stances and shapeshifting forms are exempt. |
| **Greater Party Buff Target** | Group buffs (*Prayer of Fortitude*, *Arcane Brilliance*, *Gift of the Wild*) check that targets lack both the single and group aura versions. Requires 3+ missing party members on the same map and stocked reagents. Expiring auras (<15s) count as missing. |
| **Party Target Out of Sight** | Buff, blessing, dispel, and resurrection scans prioritize visible targets before considering out-of-sight allies to avoid failed cast loops. |
| **Upkeep Buff Retry Cadence** | Out of combat buffs are throttled to at most one attempt per target per 3 s (group buffs once per minute) to eliminate cast spam when movement or mana issues occur. |
| **Duplicate Party Buffs (multi-buffer race)** | When multiple buffers are in a party, a process-wide 4 s buff claim prevents concurrent duplicate casts on the same target. |
| **Hidden GM Party Member** | Party members in hidden GM state (`VISIBILITY_OFF`) are filtered out of buff, blessing, and dispel candidate lists, preventing wasted mana on un-targetable players. |
| **Remaining Loot Contents** | Loot-content accessor excludes globally looted items; quest, free-for-all, and conditional loot evaluate against per-player candidate lists. |
| **Elite & Boss Pulls vs Selling Members** | The `can fight elite` and `can fight boss` triggers veto pulls only when a party member is actively leaving (bags 80%+ with an active vendor errand in the overworld; suppressed inside instances). |
| **Loot Distance Waste** | Corpse looting eligibility is evaluated using `Player::IsAllowedToLoot` before queueing movement, preventing bots walking toward corpses they cannot loot. |
| **Loot Timing vs. Pulls** | The post-combat loot chain checks `bot->GetAttackers()` rather than transient combat flags, ensuring fresh kills are looted immediately without kneeling during incoming add pulls. Corpses stay queued for 180 s. |
| **Loot Walk Near Unpulled Packs** | Inside instances, corpse looting requires that no live hostile is within sight range (60 yd) or that the corpse is within 3 yd, preventing looting walks from aggroing extra packs. |
| **Evading Creature Target Guard** | Creatures in evade mode (`SPELL_MISS_EVADE` / pathing failures) are invalidated via `PossibleTargetsValue::IsValid` and `InvalidTargetValue`, dropping them from targeting. |
| **First-Login Race Intro** | Fresh headless bot logins automatically terminate introductory cinematics (`BotManager::OnPlayerLogin`), avoiding invulnerability and evade loops on freshly created characters. |
| **Talent Rank Spell Persistence** | A talent rank spell must never stay *dependent*: this core writes only non-dependent spells to `character_spell` and keeps talents nowhere else. Its spell-chain pass marks the lower ranks dependent whenever a higher rank is learned, and Turtle's data heads a few trainer chains with a talent rank-1 spell (*Lightning Strike* 51387 → 52420 → 52422, *Mortal Strike* 12294 → 21551 → 21552, *Bloodthirst*, *Shield Slam*, *Moonkin Form*, *Repentance*). `PlayerbotFactory::EnsureTalentSpellsSaved` re-asserts those entries at the end of the class-trainer scan (`InitClassLevelSpells`) and on every bot login; without it a 51-point shaman tank hire kept Lightning Strike in its live spellbook but in no row, so the armory read 15/35 instead of 15/36. |
| **Ghost Corpse-Run Stall** | Dead ghosts that make no progress toward their corpse for 60 s hand off to the spirit healer. Graveyard walks apply the same check to avoid infinite ghost stalls. |
| **Corpse Run First** | Masterless bots run back to their corpse; the spirit healer takes over only on the fourth death in a row without a kill (three failed corpse runs), after a stalled run, a long ghost time, or with nothing to lose. A ghost reviving at the healer first steps about 10 yd away (its own direction, 15 s cap) so revived bots do not pile up on one spot. |
| **No PvP at Graveyards** | A world-PvP seeker never starts a fight while it or its target stands within 40 yd of the graveyard they revive at; shared graveyards turned into revive-and-kill loops. Fights already under way still answer attacks. |
| **Roaming Elites** | Solo pool bots read where hostile same-band-or-above elites are now (not only their spawn point): no grind target within 30 yd of one, and out of combat they step away from a patrolling or wandering elite that comes within 30 yd. |
| **Lowbie Graveyard Strand** | Sub-10 pool bots avoid town spirit healers and return home via homebind teleport if resurrected in areas above their level. |
| **Death Cluster & Camp Loop** | Dying 3 times within 15 minutes in a 300 yd radius triggers a 30-minute blacklist on that destination creature kind; a second trigger in an hour blacklists the geographic area. |
| **Hostile-Town Death Loop** | Masterless bots dying near enemy town guards twice within 30 minutes are relocated by post-rez rescue to level-appropriate zones away from hostile towns. |
| **Errand Death Park** | Dying twice within 15 minutes while pursuing a non-grind destination (quest giver/taker/vendor) parks that destination for 30 minutes, preventing endless errand death runs. |
| **Travel Target Move Budget** | Consecutive movement failures increment a failure counter; 6 failures retire the destination and blacklist the purpose for 5 minutes. |
| **Travel Phantom Success** | Standing within 2 yd for 5 s while moves report success counts as a movement failure, preventing path shortcut loops. |
| **Travel Route Cache** | Incomplete route searches cache questions and paths for 8 s, avoiding repetitive tile and mesh queries on every turn. |
| **Unwalkable Grind Kind Give-Up** | Mesh-proven NOPATH walks on the same map blacklist that creature kind from further grind destination consideration. |
| **One Trainer Journey at a Time** | Choosing a trainer errand sets a 10-minute cooldown on trainer re-requests unless cleared by spell training or level up. |
| **Own-Craft Trade Trainer** | Trade trainer journeys only target trainers matching the bot's known primary professions or free primary slots. |
| **One Vendor Journey at a Time** | Vendor requests are locked for 10 minutes after assignment unless cleared by a completed sale or level up. |
| **Dungeon Navmesh Islands** | Ledges and raised daises inside instances detect drop-offs (2-8 yd) and execute step-down splines when pathing is blocked. |
| **Dungeon Follow Stall** | Inside a dungeon the core follow can stand still for good on a partial path (a navmesh island or pocket): the bot shows "moving" with no motion. Out of combat, a bot more than its follow range plus 3 yd behind that has not moved a yard for 2 s while nothing holds it (eating, casting, rooted) latches a rescue (`DungeonFollowStallPolicy.h`): the ledge step-down, else the module's `MoveTo2` along the navmesh path to its follow spot, at most once per 2 s, backing off to 8 s while no walk starts, and five walks in a row that cannot start give up for a minute. Beyond sight range (`AiPlayerbot.SightDistance`, 60 yd) there is no core follow to watch: `MovementAction::Follow`'s `MoveTo` fails without a word on a partial path, so a follow there that did not start moving, inside a dungeon and out of combat, arms the same rescue at once (`ArmFar`; the same pacing and give-up). Uldaman run-leader rerun (build 19z11, 2026-10-17): the priest stood for minutes on a navmesh pocket at (43, 371) while the party walked off. Recovery: A bot that cannot be rescued (below a platform, a pocket with no path out) stays: the run leader's summon of a stuck straggler, or `.bot summon <Name>` out of combat. |
| **NoPath Trap** | Three consecutive `nopath` destinations within 10 yd over a 20-minute window trigger the unstuck rescue routine. |
| **Open-Sea Travel Standstill** | Swimming bots stranded with failed movement teleports to the nearest route node when [`AiPlayerbot.StuckTravelTeleport = 1`](../server/settings/world-population.md#aiplayerbotstucktravelteleport). |
| **Empty-Destination WORK Release** | Arriving at an empty grind spot or missing quest objective expires the WORK state after 30 s to request a fresh destination. |
| **Companion Travel & Gather in Instances** | Player companion bots automatically suppress open-world autonomous travel and gathering sweeps inside dungeons and raids. |
| **Loot Open-Failure Give-Up** | Three failed or empty open attempts on a corpse or gather node abandon the target for 120 s. |
| **Reach Give-Up Spares Group Fights** | The 15 s reach give-up timer is suppressed when the target is actively fighting any party member or pet. |
| **Melee Reach vs Melee Strikes** | Melee abilities execute whenever the target is within melee auto-attack reach, preventing reach stutter between 1.5 yd and 5 yd. |
| **Gather Cast Kept Running** | Active non-melee gathering casts suppress follow calls and re-targeting until the gathering cast finishes. |
