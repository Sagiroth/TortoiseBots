---
id: concept-bot-mechanics-and-quirks
title: Deep Bot Mechanics, Quirks & Capabilities
category: concepts
summary: Comprehensive technical deep-dive into target selection algorithms, threat distribution, movement, interrupts, resting, and known quirks.
tags: [mechanics, quirks, targeting, threat, interrupts, movement, ai]
relates_to:
  - concept-strategy-engine
  - concept-known-limitations
  - guide-player-controls
---

# Deep Bot Mechanics, Quirks & Capabilities

This document provides a thorough technical breakdown of how TortoiseBots actually functions under the hood: how targeting decisions are calculated, how movement and threat interact with the core engine, subtle gimmicks/quirks, and current implementation gaps.

---

## 1. Targeting Algorithms & Threat Distribution

Targeting in TortoiseBots is governed by specialized value calculators rather than a flat "nearest mob" check:

| Role / Intent | Target Calculator | Selection Logic |
| :--- | :--- | :--- |
| **Tank** | `TankTargetValue` (`FindTankTargetSmartStrategy`) | **Bucketed Aggro Priority:** attackers are bucketed — loose mobs the tank holds nothing on first (nearest), then held mobs in melee reach, then held mobs out of reach; among held mobs the current target wins and lowest personal threat orders the rest. `TankAssistTrigger` only switches while the tank still holds its current target (`has aggro`) and never while that held mob is at or below `AiPlayerbot.LowHealth` (50%): the tank finishes a low mob before peeling a loose add, then can switch back to anything it left. Priority 1 = Explicit `.bot action attack` target; Priority 2 = Raid Target Icon; strictly ignores CC targets. |
| **DPS (Single Target)** | `DpsTargetValue` | **Tank-First, Then Lowest Health:** Priority 1 = Explicit `.bot action attack` target; Priority 2 = Raid Target Icon (**Skull**); Priority 3 = the live group tank's current target (when not CC'd); Priority 4 = Non-CC attacker with the **lowest current health** to burn mobs down one by one. |
| **DPS (AoE)** | `DpsAoeTargetValue` | **Highest Health First:** Targets the enemy with the **highest health** so damage-over-time (DoT) effects and cleaves tick for the longest possible duration. |
| **Crowd Control** | `CcTargetValue` + `HasCcTargetTrigger` | **Exclusive mark ownership + gated CC:** one mark = one bot (assigning a mark resets other owners to `none`; `cc clear` dismisses). Never CCs over an existing breakable/unbreakable CC (own-aura re-CC still flows). Inside non-raid dungeons CC fires only on the bot's assigned mark, unless the opt-in `auto cc` strategy is ON (`.bot action auto cc [on|off]`, OFF by default, persisted per bot): then a CC-capable bot may sheep a loose add hitting a healer/caster that nobody attacks, with no periodic-damage aura, never the last mob, never skull or the tank's target. Explicit marks always win; one bot per mob and one auto target per bot; no re-sheep once DoT'd/attacked; while ON that loose add is the bot's only free pick. AoE triggers refuse packs holding a breakable CC. Otherwise **Smart Exclusions:** (1) The bot's own current target and its RTI target (the tank's target is excluded only in the auto-CC nobody-attacking rule), (2) Mobs below medium health (`AiPlayerbot.MediumHealth`, default 70), (3) Mobs inside active AoE radiuses. |
| **Grind Target (Level 1–4)** | `GrindTravelDestination` | **Beginner Band Clamp:** Bots level 1–4 clamp the *destination's* level ceiling to their own level and are permitted to target coinless starter beasts (e.g., boars, scorpids, plainstriders) while strictly excluding critters (`CREATURE_TYPE_CRITTER`). |
| **Grind Target (mob pick)** | `GrindTargetValue` | **Low-Level Order Cap:** the mob picked by `attack anything` (solo grind only). A bot below level 10 with no real player master and outside a battleground takes targets at most **1 level above** its own — lower-level and same-level mobs, quest objectives excepted; from level 10 the long-standing **+4** ceiling stands. |
| **Attack Order (auto-attack)** | `AttackAction` | **Auto-attack arming:** `bot->Attack(target, meleeAttack)` arms `UNIT_STAT_MELEE_ATTACKING`, and in this core that state *is* the auto-attack (`Player::Update` → `UpdateMeleeAttackingState`; without it the bot never swings, however close the mob is). A bot the strategy set calls "ranged" but which has no usable ranged weapon (pre-10 druid on Wrath, elemental shaman) is armed for melee anyway, so it swings once the mob closes instead of standing on it with an empty action list. Healers keep the previous hands-off handling. |
| **Enemy Healer** | `EnemyHealerTargetValue` | Detects enemies (no creature-type filter) casting interruptible healing/positive spells in range and surfaces them as high-priority interrupt/focus targets (current target excluded). |

---

## 2. Tactical Interrupt & CC Resolution Gimmicks

When you issue `.bot action interrupt` or `.bot action cc <mark>`, the server resolves executors in `commands/BotCommandContext.cpp`, then validates, executes, and falls back to a reach-queue in `commands/BotCommands.cpp` (`ExecuteInterruptAction`):

```text
Player Issues .bot action interrupt
    │
    ▼
1. Target Validation: Is target alive, hostile, and actively casting a non-melee spell?
    │
    ▼
2. Executor Selection: probes the action graph for a ready interrupt (eleven registered actions):
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
   - If out of range: Enqueues reach action on the active engine so the bot closes distance.
```

### Pet Discipline Around Crowd Control
A classic PlayerBots bug was pets breaking crowd control immediately after application. In TortoiseBots, pets belonging to Hunter and Warlock bots are kept off CC'd targets via the breakable/unbreakable CC check (`HasBreakableCC`: *Polymorph*, the frozen aura state (`IsFrozen()`, which *Frost Nova* also sets), *Sap*, *Gouge*, *Shackle Undead*; `HasUnBreakableCC`: stun/fear/roots) unless the Skull mark opts out. *Seduce* is not a bot CC executor (no CC-flagged seduction action); its aura is not in the `HasBreakableCC` set, but `seduction` is in the AoE-interlock list, so a seduced mob still blocks AoE.

---

## 3. Movement, Formations & Physics Interactions

Bot movement bridges native C++ AI directly to the core server's `MotionMaster`:

| Movement Subsystem | Implementation Details |
| :--- | :--- |
| **Formations** | Supports 10 formations: `default`/`near`, `melee`, `queue`, `chaos`, `circle`, `line`, `shield`, `arrow`, `far` (plus `custom`). The bot calculates local offsets relative to the master's orientation and updates target coordinates. |
| **Catch-Up Mount (`BoostFollow`)** | When enabled in configuration (`AiPlayerbot.BoostFollow = 1`), bots far behind the leader (beyond react distance) trigger a check-mount-state so they mount up and ride to catch up instead of trailing on foot. No speed multiplier; combat-safe. |
| **Hunter Dead-Zone Weaving** | When an enemy closes to melee, a hunter fights back with *Wing Clip* and *Mongoose Bite* / *Carve* in melee — except below level 10, where a hunter with a loaded ranged weapon keeps its `ranged` kit (nothing switches it back before level 10, and the switch removes the `ranged` strategy that gates its auto shot) and steps back out of the weapon's dead zone via the live `enemy too close for auto shot` → `flee` node; there only a hunter without a usable ranged weapon falls back to melee. The donor-ported `GenericHunterStrategy` still queues `disengage` from a trigger of the same name but that file is unregistered and no `creators["disengage"]` action exists in any context, so it can never resolve; there is no snare-gated step-back to ranged distance. |
| **Kiting & Fleeing** | Casters and healers evaluate melee proximity via the `panic` / `outnumbered` triggers. `FleeAction` only retreats (toward the master/group anchor per `MovementAction::Flee`); snare spells (*Frost Nova*, *Earthbind Totem*, *Psychic Scream*) come from independent class triggers, not from the flee action — and the flee triggers carry no "without tank threat" condition. |
| **Elevators & Transports** | Moving transports (boats, zeppelins, elevators) use `TransportTeleportType` (default `2`). Rather than desyncing on complex moving geometry, bots safely teleport from dock to dock or follow the master's transport coordinates. |

---

## 4. Sustenance, Resting & Gear Evaluation

| Mechanic | How It Works |
| :--- | :--- |
| **Simultaneous Eat & Drink** | Out of combat, bots scan bag consumables whose first spell entry has spell category 11 (food) or 59 (drink). If both health and mana are depleted, the bot consumes both simultaneously in a single rest phase. |
| **Conjured Item Sharing** | Mages out of combat automatically conjure food and water stacks and trade them to mana-using party members who have low supplies. |
| **Gear Upgrades & Scoring** | When `RandomGearUpgradeEnabled = 1`, the bot evaluates equipment by calculating spec-relevant stat weights (Strength/Agility for physical, Spell Power/Intellect for casters) from the `ai_playerbot_weightscales` dataset. Items with higher effective scores are equipped automatically, and a spec-allowed weapon replaces an equipped weapon the spec forbids (main or off hand) — e.g. a rogue that moves to Assassination trades the mace it wore under the class-default scale for a dagger. A lower armor class (e.g. cloth on a warrior) is only worn to fill an empty slot or when it scores higher. Scoring needs the first-boot caches (`AiPlayerbot.GenerateItemCaches`, and the `ai_playerbot_item_info_cache` built in memory on every start) — while they are missing, the factory only kits out high-level spawns and loot only fills empty slots. The audit never runs in combat (a level-up and a loot pickup arrive mid-fight, and the action is reachable from the combat engine through `WorldPacketHandlerStrategy`); the loadout is rewritten on the next out-of-combat audit instead. The only in-combat exception is the deliberate "attacked while fishing" weapon recovery, which labels its event. Profession tools that are weapon-class "misc" items (Mining Pick, Blacksmith Hammer, Skinning Knife, Arclight Spanner, Woodcutting Axe) are never scored as weapons, so a bot with an empty hand no longer wields a pick; the copy a profession needs is still kept and bought, and quest items stay quest items. |
| **Seed Bags, Quivers & Soul Bags** | The factory leaves one bag slot free for a hunter quiver/ammo pouch and upgrades it across level tiers (ammo is moved aside first, so a full quiver still upgrades). The equip audit hands bag and quiver items straight to the bag path instead of slot resolution — core resolves an `INVTYPE_BAG` probe to a single slot and refuses it while a quiver is worn. There the replaced bag is emptied first (an upgrade bag carried inside it is moved out too, so its live position is read again before the swap) and the swap counts as done only when the bag really landed in the slot. A warlock with no soul bag treats a looted soul bag as an equip upgrade, and a bigger soul bag replaces a smaller one; the first one evicts the smallest plain bag (contents moved out first). A quiver only ever replaces a quiver or takes an empty slot (core allows one); a plain bag never evicts a quiver or soul bag. Plain-bag upgrades are measured against plain bags only. |
| **Initial Skill & Profession Seeding** | On servers running persistent-level bots (`DisableRandomLevels = 1`), bots never pass through the legacy `Randomize()` pipeline. To avoid swinging with weapon skill 1/5 and having no trade skills, fresh random bots receive their full suite of class weapon skills (scaled to current level cap), First Aid, and two class-compatible primary professions once on initial login. |
| **Selling Loot for Spell Money** | A bot that cannot pay for the cheapest green class rank at its trainer (`should sell`, below the usual 80% bag trigger) converts loot to coin — but only when the trip is worth it: the vendor-usable stock must cover the money missing for that rank, or at least 8 items have piled up while the bags are 60% full. The detour is also suppressed inside instances and under an active player master. At the vendor such a trip liquidates the whole vendor stock instead of the usual random 20–80% of it, so the bot does not walk home still short and get sent straight back. Without those rules the bot walks to town for one pelt, is still broke, and walks back. |
| **Full-Bag Cleanup Order** | At 90% bag capacity `smart destroy item` throws away, in order: items with no use at all, quest items (only while the bot needs money and cannot sell), then grey vendor trash cheapest-first, then profession stock, and only then consumables (food, water, potions). Being short on a spell rank therefore never costs a bot its healing potions. |
| **Elite & Boss Pulls vs. Selling Members** | `can fight elite` / `can fight boss` are vetoed by a group member only when that member is really about to leave — bags past 80% and a vendor trip available, in the overworld. A member that merely plans to liquidate its loot at the next vendor never freezes the party, and inside an instance nobody is sent to a vendor mid-run. |

---

## 5. Known Quirks, Gaps & Edge Cases

An honest accounting of where the engine currently stands and where future work is needed:

| Subsystem / Quirk | Technical Reality & Current Behavior | Recommended Player / Operator Action |
| :--- | :--- | :--- |
| **Line-of-Sight & Doorways** | On complex multi-level geometry (e.g. Blackrock Depths stairs), pathfinding can occasionally hitch if line-of-sight is lost. | Use `.bot action come` or `.bot summon` to snap bots to your location. |
| **Warlock Life Tap Suicide Risk** | While safety guards suppress *Life Tap* below 50% health, high incoming ambient raid damage can occasionally catch a tapping bot before a heal lands. | Keep healer bots set to higher reaction urgency (`AiPlayerbot.LowHealth = 65`). |
| **Shaman Totem Churn** | When pulling through large dungeons, Shamans may leave totems behind, re-dropping them as new combat anchors are established. | Normal behavior; Shaman mana regenerates quickly during out-of-combat drinking. |
| **Upkeep Buff Retry Cadence** | Out of combat an upkeep buff (self or party, any class) is attempted at most once per target per 3 s, and every applied self buff writes a rate-limited `SelfBuff` row to `bot_events.csv` (spell name + bot level). The aura check still decides whether the buff is needed: an active aura is never overwritten, and each group member is a separate target, so group buffing is unaffected. Before this, a buff whose aura was still missing after the attempt (drinking, movement, range/LOS, not enough mana) was re-attempted on every AI tick for bots owned by a player — the engine's failure backoff exempts them, so a hired priest could look like it was "buffing itself, drinking and repeating" (issue #359). | Count `SelfBuff` rows per bot and spell in `bot_events.csv`: about two per hour for a 30-minute buff is healthy; a row every few seconds means the buff never sticks — check mana, range and whether the bot is being asked to move at the same time. |
| **Loot Distance Waste** | Personal-loot eligibility is known up front for creature corpses: the bot applies the core's own `Player::IsAllowedToLoot` gate — round-robin turn, free-for-all, master loot, the allowed-looter set recorded at the kill — before queueing a corpse, so it no longer walks to loot that belongs to someone else. Two clauses are trimmed on purpose: another member's over-threshold drop no longer pulls the whole party to the corpse (bots answer the roll wherever they are; the turn holder opens it), and under master loot only the master looter (or a bot with personal quest/FFA loot) opens corpses, in the open world too. Game objects have no equivalent predicate (the core `loot.CanLoot` call is stubbed), so a bot may walk up to a node before discovering nothing is lootable. | Purely cosmetic movement; does not impact combat or party progression. A corpse whose assigned member is a human who never opens it stays unlooted — same as a player group in 1.12; the items are hidden from everyone else by the core, and opening/closing the corpse releases the rest to the bots. |
| **Loot Timing vs. the Next Pull** | The loot chain ("loot" 6.0 > "attack anything" 5.0) is meant to open the bot's own kill before it pulls again, and it only exists in the non-combat engine — whose state is driven by the "combat start"/"combat end" reaction (`has attackers`), not by `UNIT_FLAG_IN_COMBAT`. The flag lingers a moment after every kill, and testing it inside `loot available` suppressed the loot chain for exactly that window, so the bot ordered the next pull and the corpse expired unopened; the same value also counted any group member in combat within 150 yd. The gate is now the core's own attacker set (`bot->GetAttackers()`, filled the moment an add aggros and drained the moment it dies), so a fresh kill loots at once and the bot never kneels next to an incoming add. A queued corpse is now kept for 180 s (and its age is refreshed on every re-add, including the kill's own `SMSG_LOG_XPGAIN` add) so a fight can interrupt the loot chain without losing the corpse. | None — bots loot after the fight instead of losing the corpse. |
| **Self-Botting Limitation** | You cannot turn your currently logged-in human character into a bot; bots must be secondary headless characters from your account. | Spawn secondary characters from your account using `.bot add`. |
| **Evading Creature Held As Target** | A creature in evade mode cannot be killed: the core refuses to start an attack on one (`Unit::Attack`), drops every point of damage aimed at it (`WorldObject::DealDamageMods`, `Unit::DealMeleeDamage`, `SpellHitResult` → `SPELL_MISS_EVADE`) and regenerates it while it walks back to its spawn point — the reported "mob HP regenerates, it cannot be killed". Bots now neither pick nor keep one: the evade predicate sits in `PossibleTargetsValue::IsValid` (grind picks, pulls, the travel/area scans and the "possible targets no los" test that clears a stale `attack target`), in `PossibleAttackTargetsValue::IsValid` (dps/tank assistance), in `AttackersValue::IsValid` and in `AttackAction::IsTargetValid` (the last gate before `bot->Attack()`, which covers a creature that entered evade after the target value was cached), and `InvalidTargetValue` drops a target already held. The creature reaching evade at all is the core's own reset (`Unit::SelectHostileTarget` → `OnLeaveCombat` → `EnterEvadeMode`), not a bot order: it fires when the last attacker of a creature that is still flagged in combat stops being a valid target — the bot died, left the world (pool logout), or every threat reference fell outside the threat radius around the position the fight started at. No module action is needed. | Nothing to do when a mob resets home; it comes back to full health at its spawn point and is attackable again once it stops moving (a bot will pick it up then). To attribute the cases that remain in the log, the `EvadeProbe` row in `bot_events.csv` carries `motion` (6 = HOME, sent home by `EnterEvadeMode`), `homedist` (how far the fight dragged it from the spawn point), `csdist` (how far the fight moved from where it started — the threat-radius check), `atk`/`threat` (who, if anyone, still holds it) and `petvictim` (a pet was the one fighting it), alongside the health and distance fields. |
