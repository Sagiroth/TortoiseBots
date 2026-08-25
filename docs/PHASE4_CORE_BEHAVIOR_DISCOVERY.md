# Phase 4 — Core Behavior Discovery (first playable vertical slice)

**Status:** historical discovery archive — superseded by the current native module implementation
**Date:** 2026-08-21  
**Scope:** `human + owned bot` → follow / stay / assist / auto-attack / return-to-follow / clean remove  
**Plan ref:** `docs/PLAN.md` §12, §26 — Phase 3 is closed (host seam, headless login, queued AddSession, reclaim) — do not modify Phase 3 architecture here  
**Sources pinned:** `cmangos-playerbots@076045e` (`2026-08-16`), `mangoszero-server@1817ae1`, `shyalya-tortoise-wow@1f9497e` (vendored `cmangos-playerbots@c33dfac` via `r-o-sh@0af2567`), `cmangos-mangos-classic@9b682be`, `TortoiseWoWKnowledgeBase` baseline `172ee948e... @2026-08-12`, `Penqle/tortoise-wow` baseline via local `tortoise-docker-penqle/source` (`d07ec3fe`)

> **Rule:** harvest behavior, not architecture. Every recommendation below keeps bot-specific meaning inside the `TortoiseBots` module; the core only knows `SessionTransport::Headless` / `IWorldUpdateListener`.

> **Archive notice:** the proposed `BotController`/helper architecture in this document was not adopted. Current movement and gameplay are owned by mature `PlayerbotAI`; `BotManager` owns records and adapters, while `World` owns sessions. Use `docs/PLAN.md` and the current sections of `docs/HOST_API.md` as implementation authority.

---

## 1. Executive summary — what a basic bot fundamentally needs

A playable bot is five things, not a framework:

1. **A tick that is not a framework.** The legacy `PlayerbotAI::UpdateAI` is throttled by `reactDelay` (100 ms), `passiveDelay` (4000 ms) and a `globalCoolDown` (500 ms) and driven from the world/map thread. TortoiseBots already has the correct seam for this: `IWorldUpdateListener::OnWorldUpdate(diff)` called once per `World::Update` frame. The whole MVP can run from that single tick — no per-`Player::Update` hook, no background thread touching `Player`/`Map`.
2. **A persistent intent, not a transient action.** `follow` is the only persistent movement intent worth keeping. `stay` is "follow with a saved anchor". `attack` is a *temporary* intent that suspends the persistent one and returns to it when the target dies or becomes invalid. Everything else (formations, guard, wander, travel) is deferred.
3. **One distance rule.** `followDistance = 1.5y` (CMaNGOS config, `PlayerbotAIConfig.cpp:140`), not `30y`. The bot starts a `MoveFollow` when its 2-d distance to the follow target exceeds that `followDistance` and the target is not taxi-flying/dead/self. It does not replace an already-moving native follow generator targeting the same unit (see `FollowAction::isUseful`, `FollowActions.cpp:86-93`). That tight loop is the whole MVP: nothing else fixes "bot lags 30 yards behind".
4. **One target rule.** MVP bots do not invent targets. They *assist* the owner. If the master's current selection/attack target is a valid hostile within `sightDistance` (75 y, `aiplayerbot.conf.dist.in`), the bot adopts it. Otherwise no target. Validation is `PossibleAttackTargetsValue::IsValid` (alive, in-world, same map, not friendly, not untargetable, within range, LOS where required). That rule alone covers every acceptance case the task lists (master changes target, master clears target, target dies, target unreachable).
5. **One combat rule.** When a target exists, the bot stops following, faces it, sets selection, optionally sends pet to `COMMAND_ATTACK`, and calls `bot->Attack(victim, !isRanged || dist<5)` — vanilla `Unit::Attack` that arms auto-attack. Melee approach is `MoveChase`/`reach melee` triggered by `enemy out of melee` (from `MeleeCombatStrategy`). When the target dies or `invalid target` fires, the bot calls `AttackStop`, clears `current target`, adds the corpse to `available loot` (legacy `SelectNewTargetAction`), and resumes follow. No rotation, no spells.

The 15-year lesson is that the visible quality comes from **target validation, return-to-follow, and not rebuilding trigger graphs every tick** — not from 450 actions or 137 chat triggers. The MVP deliberately ships one intent (`Follow(anchor?)`), two movement verbs (`MoveFollow`, `MoveChase/Point`), one target verb (`AssistOwner`), and two combat verbs (`Attack`, `AttackStop`) — plus the tiny bookkeeping to suspend/resume the intent.

---

## 2. Source comparison

| Capability | CMaNGOS (076045e) | MangosZero (1817ae1) | Shyalya (1f9497e) | Recommended for TortoiseBots MVP |
| --- | --- | --- | --- | --- |
| **Follow** | `FollowMasterStrategy` + `FollowAction` + `Formation` (`followDistance 1.5y`, angle/offset, `MoveFollow`, formation `GetLocation()`); multi-strategy dance (`follow` vs `stay` vs `wander` vs `guard`); `MoveTo2` / `TravelNode` pathing + `FlyDirect` | `FollowActions.cpp:Follow(Unit*, dist, angle)` → `MoveFollow` when `distance > followDistance`; simpler `MoveTo(Unit*, distance)` with `GetNearPoint` + LOS loop; `MovementAction::Follow` checks `IsAggroPosition`, transport, map change, far-distance `TeleportTo` shortcut | Same lineage as CMaNGOS (`c33dfac` snapshot) + Turtle shims: `NullSessionAnticheat`, `~80` host hooks, shyalya `follow jump` strategy, BG queue `recursive_mutex`, 125 y healer range, talent rework | **Minimal:** `MotionMaster::MoveFollow(master, 1.5f, angle)` with angle from master's orientation (`GetFollowAngle`). Dead-zone = `followDistance`. Recompute only when master's move changes `> targetPosRecalcDistance` (default ~ platform). Defer formations to "near" only. Keep the `isUseful` guard (don't restart same follow). No `TravelNode` graph — use direct `MoveFollow` (navmesh does the work). |
| **Stay** | `StayStrategy` (`GetDefaultNonCombatActions: "stay"`), `StayAction::Stay` (snapshot `PositionMap["stay"]` + stop moving; `return to stay position` trigger) | `StayActions.cpp:Stay()` clears motion, `ChangeStrategy("+stay,-passive")`; `StayChatShortcutAction: "+stay,-passive"` vs follow's `"+follow master,-passive"` | Identical to CMaNGOS, but `StayChatShortcutAction` also does `+stay,-follow,-wander` + sets `PositionMap["stay"]` | **Simpler:** `stay` = `persistent intent = Stay(anchorPos)` where `anchorPos = bot pos at command time`. No separate `StayStrategy`. While Stay, movement only on combat or explicit `follow`. `follow` clears the anchor. Combat does NOT cancel stay — bot returns to anchor after `invalid target`, not to master. That matches the task's "simplest MVP semantics" and avoids the legacy ambiguity where stay was both a strategy and a position value. |
| **Assist / target selection** | `FollowTargetValue` → `master target` or `formation->GetTargetName()` or `manual follow target`; `DpsAssistStrategy` (`"not dps target active"` → `"dps assist"` → `DpsTargetValue` → least-HP attacker); `AttackersValue` builds `possible attack targets` via `ThreatManager` + `sightDistance` grid scan + `PossibleAttackTargetsValue::RemoveNonThreating` (CC-aware); `InvalidTargetValue` gates | `AttackersValue::Calculate` → `AddAttackersOf(bot)` + `AddAttackersOf(group)` + `RemoveNonThreating` (hasRealThreat, visible, LOS); `AcceptUnit` via `IsValid` (not dead, braced flags, `CanAttack`); simpler — no `AttackersValue::IsValid` share-bots copy | Same as CMaNGOS; additional targeting patches (stealth, healer 125 y, Rti CC) | **MVP:** single `AssistOwner` value: `master->GetSelection()` if that `Unit*` passes `IsValidHostileForAssist` (hostile, alive, same map, within 75 y, `CanAttack`, not `UNIT_FLAG_NOT_ATTACKABLE_1`/`UNTARGETABLE`), else `nullptr`. No scanning, no threat sharing. This is what the vertical slice tasks require: owner's current target. Owner's attacker is deferred to dungeon MVP. |
| **Attack / auto-attack** | `AttackAction::Attack(requester, target)` validates (`IsTargetValid`), `SetSelectionGuid`, `SET_AI_VALUE("current target")`, `SetFacingTo` if not `IsInFront`, `AttackStop` on shield check (≥10% max HP thorns/reflect), `bot->Attack(target, !isRanged or dist<5)` enables auto-attack, `OnCombatStarted`;`MeleeCombatStrategy`(`enemy out of melee` → `reach melee`;`enemy too close for melee` → `move out of enemy contact`);`SelectNewTargetAction` clears `current target`,`AttackStop`, loot | Same skeleton, lighter: `ChatShortcutActions` drive `ChangeStrategy("+/-stay","+/-follow")` then direct `Attack` call — no `AttackAction::PetAttack` CC complication | CMaNGOS lineage + Turtle spell fixes (druid forms, `War Stomp` distance 8 y, `thorns`/`reflect` stop, `IsPositiveAuraEffect` purges); extra shim `cmangos-compat-shim.h` | **MVP melee only:** `Combat::Start(target)` → `bot->SetSelection`, `Store oldTarget`, `InterruptSpell`, `MoveChase(target)` until within `ATTACK_DISTANCE` (~5 y), then `bot->Attack`. `Stop`: `AttackStop`, clear selection, `pet->COMMAND_FOLLOW` if applicable. No ranged/spell work. Shield/immune checks come from recent fix `e9e7009` — preserve that guard (skip attacking a ≥10% thorns target). |
| **Commands** | `ChatShortcutActions`: `follow` → `+follow,-passive,-stay,-wander` + `PositionMap["return"].Reset()`, `stay` → `+stay,-follow,-wander,-passive` + `SetPosition(bot,"stay")`; console `.bot add/login/remove/logout/summon` via `PlayerbotMgr` | `FollowChatShortcutAction: "+follow master,-passive"` / `StayChatShortcutAction: "+stay,-passive"` — strictly two intents toggled per `BOT_STATE_*` | Same as CMaNGOS + `AllowGuildBots`/`AllowMultiAccountAltBots` ownership gates; `.bot` returns `ok` before login reaches world | **MVP chat is module-only:** `.bot follow` → `controller.SetIntent(Follow{masterGuid})`, `.bot stay` → `controller.SetIntent(Stay{anchor})`, `.bot attack [targetGuid]` → `controller.AssistOrAttack(guid)`, `.bot stop` / target death → clear combat target but keep persistent intent. Chat triggers are parsed in module on `WorldSessionScript::OnPacket` or module-owned `HandleChat`; do not add a per-command core hook. |
| **Update / thinking** | `PlayerbotAI::UpdateAI` every `Player::Update` → `aiInternalUpdateDelay` (init `reactDelay=100ms`, after cast `castTime+reactDelay`, combat wake resets, minimal mode `reactDelay*10`), `DoNextAction(minimal)` → `Engine::ProcessTriggers` → `ActionBasket` queue → `globalCoolDown=500ms`, `passiveDelay=4000ms` minimal, `IterationsPerTick` bounds | `PlayerbotAIBase::nextAICheckDelay` (`globalCoolDown`, `maxWaitForMove`), `UpdateAI` → `UpdateAIInternal` via single `OnPlayerUpdate` / world tick | Same as CMaNGOS | **MVP cadence:** drive from `IWorldUpdateListener` once per world tick. Per-bot `Update(diff)` with fixed 100–200 ms throttle (`reactDelay`). Non-combat/idle bots use 1–2 s throttle. No `IterationsPerTick` queue — explicit `switch(intent)` is the MVP engine. Expensive queries cached: `CanAttack`, `sightDistance` check, `Unit::CanAttack` is the cache. |
| **Movement backends** | `MovementGenerator`: `FOLLOW_MOTION_TYPE`, `POINT_MOTION_TYPE`, `CHASE_MOTION_TYPE`; `MoveTo2` resolves `TravelNode` graph + `getPathTo` navmesh + `MovePoint`/`MovePath`/`MoveFollow` | Same generators, simpler `MoveTo` using `GetNearPoint` + LOS tries + direct `MoveFollow(target, dist, angle)` | Same + Turtle navmesh tile keep | **Reuse vanilla generators:** `MoveFollow` for follow, `MoveChase` for combat approach, `MovePoint` for stay return. No transport/teleport special-casing in MVP; just check `HasUnitState(UNIT_STAT_ON_VEHICLE)` early-return. |
| **State/intent** | `BotState` enum (`COMBAT 0, NON_COMBAT 1, DEAD 2, REACTION 3, ALL`); 4 engines each with strategy/trigger sets; `AiObjectContext` with 195 `Value<T>` + strategy contexts | Same skeleton, but `ChatShortcutActions` mutate only `follow master`/`stay` per `BOT_STATE_*` — simpler visible intent | Same as CMaNGOS | **MVP state:** `enum class Intent{Follow, Stay}` persistent + `struct CombatState{ObjectGuid target; enum Phase{Approaching,Attacking}}` transient. `dead` is implicit (`!IsAlive` → stop movement/combat). No `Reaction` engine. `previousIntent` stack depth 1 is enough for `combat → previousIntent`. |

---

## 3. Legacy architecture map

### PlayerbotAI (CMaNGOS `playerbot/PlayerbotAI.h` / `PlayerbotAI.cpp`)

Central per-bot brain. Owns `bot` (`Player*`), `master` (`Player*`), `AiObjectContext`, `4×Engine` (one per `BotState`), `FleeManager`, chat packet helpers, jump/teleport state. Tick entry `UpdateAI(elapsed, minimal)` (throttled by `aiInternalUpdateDelay`) → `UpdateAIReaction` (reaction engine) → `UpdateAIInternal` → `DoNextAction`. Combat transitions call `OnCombatStarted` / `Reset`. **Classification:** architecture debt — a 6000-line God object that mixes lifecycle, combat, movement, and chat. **Useful concept:** throttled entry + combat-state switch. **Do not copy.**

### Managers

* `PlayerbotMgr` / `PlayerbotHolder` / `RandomPlayerbotMgr` — cmd routing (`.bot add/login/remove`, security gates `INVITE`/`ALLOW_ALL`, target grammar), holder→bot `PlayerbotAI` ownership, random-pool scheduling. TortoiseBots' `BotManager` + `BotSessionAdapter` already replace the ownership/session piece in a cleaner way (transport enum, queued `AddSession`, pending-cancel, reclaim). **Debt:** global `sPlayerBotMgr`, scattered `PlayerbotMgr` pointer on `Player`. **Keep:** a single `BotManager` owned by the module, not on `Player`.

### Strategy (CMaNGOS `strategy/Strategy.h`, `StrategyContext.h`, `generic/*.cpp`)

A strategy is "a set of default actions + triggers + multipliers for one `BotState`". Examples: `FollowMasterStrategy` (triggers `out of free move range` → `follow`, `update follow` → `follow`), `StayStrategy` (`return to stay position` → `return to stay position`), `DpsAssistStrategy` (`not dps target active` → `dps assist`), `MeleeCombatStrategy` (`enemy out of melee` → `reach melee`). Strategies are composed dynamically via `co`/`nc`/`de`/`react` chat actions (`+name`/`-name`). **Useful later:** role strategies (tank/heal/dps) deserve a small composition mechanism. **Debt:** ~137 generic strategy keys + 643 class keys, rebuilt into trigger lists inside `Engine::Init()` every strategy add/remove — measured as the trigger-rebuild perf bug (105M rebuild/hour in DISCOVERY §9). MVP does not need this.

### Trigger (CMaNGOS `strategy/Trigger.h`, `triggers/GenericTriggers.cpp`)

Poll-based predicate checked inside `Engine::ProcessTriggers(minimal)`. If `IsActive()`, push `NextAction::array(...)` onto the `ActionBasket` queue. Examples relevant to MVP: `InvalidTargetTrigger` (`AI_VALUE2(bool,"invalid target","current target")`), `DpsAssistTrigger`, `NotDpsTargetActiveTrigger`, `EnemyOutOfMeleeTrigger`, `CombatStuckTrigger`. **Useful concept:** the *predicates* (invalid target, has attackers, out of melee) are worth preserving as pure functions. **Debt:** trigger objects allocated per-strategy, sorted by "relevance", evaluated every tick even when irrelevant; many triggers duplicate `Value` logic.

### Action (CMaNGOS `strategy/Action.h`, `actions/*.cpp`)

Named executable unit with `Execute`, `isPossible`, `isUseful`, `GetRelevance`. Examples: `FollowAction`, `StayAction`, `AttackAction`, `ReachMeleeAction`, `SelectNewTargetAction`. `Engine::DoNextAction` pulls highest-relevance basket, validates `isPossible`/`isUseful`/`multipliers`, then executes one. **Useful concept:** narrow, testable verbs (`Follow`, `Attack`, `ReturnToStay`). **Debt:** 450 normal-build shared actions + 675 class actions; many actions contain branching that belongs in `Value` predicates.

### Context / Value system (CMaNGOS `strategy/Value.h`, `ValueContext.h`)

`Value<T>` = lazily cached computed fact (`AI_VALUE(T,"name")`). ~195 concrete values + 332 creators. Examples: `FollowTargetValue`, `CurrentTargetValue`, `InvalidTargetValue`, `AttackersValue`, `PossibleAttackTargetsValue`, `LeastHpTargetValue`. `AiObjectContext::Update()` was disabled for being an 8% idle no-op. **Useful concept:** cached perception facts are essential for performance (no grid scan every tick). **Debt:** global string-keyed registry (`"follow target"`, `"current target"`, `"attack target"`), late-bound via `AI_VALUE` macros, hard to grep or type-check. Keep the *idea* (values with `checkInterval` + `Expired` + `Recalculate`), not the string registry.

### Movement (CMaNGOS `MovementActions.cpp`, `FleeManager`, `TravelMgr`)

Two layers: simple `MovementAction::MoveTo`/`MoveNear`/`MoveFollow`/`Follow` wrappers around `MotionMaster::MoveFollow` / `MovePoint` / `MoveChase`, and the heavier `TravelMgr`/`TravelNode`/`WorldPosition` graph (A* over walk/areaTrigger/transport/flightPath/teleport). Legacy MVP used `MoveFollow(target, distance, angle)` with angle from `GetFollowAngle()` (randomized per-bot offset to avoid stacking). **Useful:** `MoveFollow` + navmesh is sufficient. **Debt:** `MoveTo2` / `FlyDirect` / transport/teleport handling + full travel-graph belongs to random bots and multi-expansion travel, not owned dungeon bots.

### Command handling

Legacy parsed **137 chat trigger keys** and **129 destination commands**, `AiPlayerbot.CommandSeparator` (`\`), `@` audience filters (`@tank/@heal/@dps/...`), and `silent`/`debug` strategies that could suppress tells. `.bot add` returns `ok` before login reaches world (asynchronous). **Useful:** command→intent translation (not command→action). **Debt:** synchronous `d`/`do` immediate execution mixed with queued `cmd`, `queue <cmd>` timing hacks, `silent` dedup queue (10–20 s out-of-combat chat delay), and the 137-key prefix parser.

### Targeting / combat

`TargetValue::FindTarget(FindTargetStrategy*)` scans `attackers` (threat-derived) and chooses by least-HP, most-HP, CC, etc. `PossibleAttackTargetsValue::IsValid` is the real gate. `AttackAction::IsTargetValid` guards friendlies, dead, far (`sightDistance`), and then `bot->Attack()` enables auto-attack. **Useful:** the validation predicate and the "assist owner's target else none" policy for owned bots. **Debt:** threat-sharing between nearby friendly bots (`AttackersValue::shareTargets` copies from nearby bot's `attackers` value) — clever but couples bots via string keys and adds hidden grid scans.

---

## 4. Core transferable behavior — rules worth preserving verbatim

### 4.1 Follow

* **Distance:** `followDistance = 1.5y` (source `PlayerbotAIConfig.cpp:140`, `sightDistance=75.0f:122`, `reactDistance=150.0f:126`). This is the dead-zone: inside it, the bot does not replace an already-moving native follow generator targeting the same unit (the module cannot inspect Penqle's private requested angle/offset fields). MangosZero's `Follow` uses the same `sPlayerbotAIConfig.followDistance` gate (`MovementActions.cpp:552`). Legacy 30 y "follow" was for random/remote travel — not for owned follow.
* **Restart guard:** use the public `MotionMaster::GetCurrent()` targeted-generator target plus the active moving state; do not compare against fabricated zero offsets or private donor fields (`FollowActions.cpp:86-93`).
* **Preconditions:** `!CanMove() → false`, `target is taxi-flying → false`, `target==self → false`, `rpg target active → false`, `!IsSameMap || sightDistance exceeded → true (needs follow)` (`FollowActions.cpp:36`).
* **History lesson:** `a07286a9 Follow: Allow follow across maps`, `78bebbeb` ping-pong follow test, `b6f31474` bots trying to move around targets from long distance → pathfinder miss, `93ea35c8` rpg-vs-travel oscillation, `90910934` wander stopping near master. Common theme: follow must be *strictly tied to the master's position/orientation*, not to a stale cached path.

### 4.2 Stay

* **What legacy stay does:** snapshots `bot` position into `PositionMap["stay"]` (`StayChatShortcutAction → SetPosition(bot,"stay")`), clears `PositionMap["return"]`, adds `StayStrategy` which defaults to `NextAction("stay",1.0)` and trigger `return to stay position → return to stay position`. `StayActionBase::Stay` stops movement if moving, sets `last movement Set(NULL)`, refuses if taxi-flying. The bot may still attack while staying — `InitCombatTriggers` also pushes `return to stay position` in combat.
* **Transferable rule:** the *position* matters more than the *strategy*. MVP stays should remember one `WorldLocation anchor` (map+xyz) at the moment `stay` was issued. `return to stay position` in legacy was the correct idea (regain anchor after combat). Keep that, drop the generic strategy bookkeeping.
* **Resume:** `follow` is stay's undo: `ChangeStrategy("+follow,-passive,-stay,-wander", NON_COMBAT)` + `PositionMap["return"].Reset()` (`ChatShortcutActions.cpp:39`). That reset is important — a stale `"return"` pollutes the next `MoveTo` after a teleport.

### 4.3 Assist / target selection

* **Priority for owned DPS (MVP):** `RtiTarget` (Raid Target Icon) if present → `master target` (master's current selection) → none. CMaNGOS `DpsTargetValue::Calculate` is literally `if(rti) return rti; FindLeastHpTarget` over `attackers`. For owned bots that lack a broad attacker scan, `"master target"` is the correct fallback — it is already filtered to hostile living enemies by the owner's client, and the bot re-validates.
* **Validation (`PossibleAttackTargetsValue::IsValid` / `PossibleTargetsValue::IsValid`) is non-negotiable:** target must be `IsInWorld() && same MapId && !UnitIsDead && !IsFriendlyTo && !UNIT_FLAG_NOT_ATTACKABLE_1/UNTARGETABLE/UNINTERACTIBLE(patched) && CanAttack && !SpiritOfRedemption aura` and within `sightDistance` and LOS where checked. Recent fixes `998c521e Check All school immunity`, `c32d4fb4 immunity mask`, `993bfc5c Don't cast damage if immune`, `e9e7009c stop vs thorns/reflect ≥10%`, `b4f61487 War Stomp useful distance in PvE` prove that validation gates accumulated the most fixes — keep them.
* **CC respect:** `PossibleAttackTargetsValue::RemoveNonThreating` already segregates breakable/unbreakable CC targets. Not needed for MVP single-target assist; defer to class/CC slice.

### 4.4 Basic combat / auto-attack

* **Start:** `AttackAction::Attack` → `IsTargetValid` (friendly/dead/far checks) → `Unmount` if mounted and `<40y` or flying → `SetSelectionGuid(target)`, `SET_AI_VALUE("current target", target)` + `Save oldTarget`, `Add to available loot`, handle pet `COMMAND_ATTACK`/`REACT_PASSIVE` based on `wait for attack`, `SetFacingTo` if not `IsInFront(...CAST_ANGLE_IN_FRONT)`, `AttackStop` guard vs high thorns, `PlayAttackEmote`, `bot->Attack(target, !isRanged||dist<5)` (melee flag), `OnCombatStarted`. That sequence matters; reordering loses emote, pet, or facing.
* **Approach:** `MeleeCombatStrategy::InitCombatTriggers` → `enemy out of melee → reach melee (ACTION_MOVE)` and `enemy too close → move out of enemy contact`. `reach melee` uses `TargetedMovementGenerator` with `offset = ATTACK_DISTANCE ~5y` and `MovementGeneratorType == CHASE_MOTION_TYPE`.
* **Stop:** `SelectNewTargetAction::Execute` on `invalid target` → push dead target to `LootObjectStack`, clear `attack target` if not in `possible targets no los`, save `old target`, `Set current target nullptr`, `SetSelectionGuid(ObjectGuid())`, `InterruptSpell`, `AttackStop`, stop pet with `COMMAND_FOLLOW`. Then if `has attackers`, DPS bots re-run `dps assist`. No `AttackStop` is a leak — Shyalya's live population tracks many bot-stuck-attacking states due to missing invalid-target clears.

### 4.5 Cross-cutting lessons from history (July–Aug 2026 deltas, c33dfac..076045e, 87 commits)

* The highest-signal fixes were all tiny predicates: `skip healing on -100% healing debuff` (`3e8ade46`), `druid form requirements` (`d844a9c7`), `don't cast if immune` (`993bfc5c`), `thorns/reflect stop` (`e9e7009`), `CC: only marked RTI` (`80e897d8`), `War Stomp within 8y` (`b4f61487`), `tooCloseToCast false if flee range too low` (`0ddc94eb`), `GCD redundant reactDelay` (`d5d48054`). None required a new engine.
* Movement regressions (`9f2776f8 flight back-and-forth`, `9889b053 loops in wotlk`, `b64bca43 wander type`) came from re-entrant strategy switches, not from `MotionMaster` bugs — the cure was fewer strategies, not more generators.
* Turtle tree breakage came from talent/spec re-generation (`LevelingDruidStrategy.cpp` shim) and spell-aura rank changes — not from chat/movement. The compatibility cost is data, not architecture.

---

## 5. Architecture debt to avoid — be specific

1. **No string-keyed registries.** Legacy `ValueContext` (332 creators), `TriggerContext`, `StrategyContext` (137+643 keys) route `AI_VALUE(T,"follow target")` through a global `std::map<std::string, Value*>`. This defeats grep, refactoring, and type-checking, and the rebuild-every-tick perf bug follows directly from it. TortoiseBots counterpart: plain structs and typed handles — `OwnerState{ObjectGuid selection}`, `CombatState{ObjectGuid current}`, `Perception::NearestHostiles(range)` as functions, not names.
2. **No reintroduction of `sPlayerBotMgr` / `GetBot()` / `m_bot`.** `AGENTS.md` bans these. Legacy spread 80 host files touching `Player.cpp`/`Unit.cpp`/`Spell.cpp`/`MovementHandler.cpp`/`WorldSession.cpp` just to ask `IsBot()`. The MVP must prove it with the existing seam (`SessionTransport`, `IsHeadless`, `NullSessionAnticheat`, `IWorldUpdateListener`) plus ONE module-owned `BotManager`.
3. **No global Strategy/Trigger/Action engine for the MVP.** The baseline `cmangos-playerbots` has 450 shared + 675 class actions, 4 engines per bot, `ActionBasket` queue, multipliers, `IterationsPerTick`, and `ProcessTriggers` that recompute every trigger each tick. This is where the 105M trigger rebuilds/hour and the "derive buffs not casting group buffs" regressions came from. For follow/stay/assist/attack we need four pure predicates, not four engines.
4. **No `TravelMgr`/`TravelNode` graph for owned bots.** `TravelTarget` (`AiObjectContext.h:53`), `FutureDestinations`, `A*` over 8 node types (walk/areaTrigger/transport/flightPath/teleport) exists to let random bots teleport across continents over days (`RandomBotTeleportMin/MaxInterval 1–7 days`). Owned bots need none of it. Reusing it in `FollowAction::isUseful` ("if rpg target, don't follow") caused the rpg→travel oscillation `93ea35c8`. Keep travel behind a separate decision boundary — the MVP only needs `MotionMaster::MoveFollow` / `MoveChase`.
5. **No chat-command prefix parser as behavior.** The 137-key `ChatTriggerContext` with `CommandSeparator` (`\`), `CommandPrefix`, `queue <cmd>` whisper-excluded, `#a` addon indirection, and 10–20 s BOT_TEXT queue delay is an undebuggable shell. Commands for the MVP should be `ChatHandler` module commands (`.bot add|remove|follow|stay|attack`) that set controller intent, with optional `SAY/YELL/WHISPER` aliases routed through a single `WorldSessionScript::OnPacket` filter — not a 137-key table.
6. **No per-tick DB or full-world scan.** MangosZero's 2026-08-20 correctness campaign explicitly fixed "hunter pet `SELECT` 10/sec, `NearestGameObjects` 20/sec idle". The invariant is: `Update(diff)` takes no SQL, no `Cell::VisitAllObjects(player, searcher, 100y)` per tick. For MVP: cache `followDistance/sightDistance/contactDistance` at startup, read `master->GetSelectionGuid()` and `sObjectAccessor.GetUnit(bot, guid)` directly (O(1) by GUID), and only run a bounded grid scan for `has attackers` when already in combat.
7. **No synchronous LLM/network on map threads.** The plan bans this, and the legacy tree learned it (Shyalya stripped `Remove LLM network client` for this reason). MVP diagnosis: an in-memory ring-buffer of decisions (time, bot, intent, target, movement goal, reason) sampled out-of-band; no HTTP.
8. **No `Player` field bloat.** Keep `BotController` as a module-owned object keyed by `ObjectGuid`, not a field on `Player`. `Player::GetPlayerbotAI()` being the canonical holder lookup is how MangosZero/cmangos couple lifetime to `Player`. TortoiseBots already keeps `BotRecord` in `BotManager` keyed by guid; extend that with `BotController` there.

---

## 6. Recommended minimal TortoiseBots behavior architecture

```text
TortoiseBots module (shared lib, linked into mangosd when BUILD_PLAYERBOTS=ON)

host/BotHostAdapter      — implements IWorldUpdateListener::OnWorldUpdate(diff),
                           owns BotManager lifetime, registers via
                           GetPendingWorldListenerFactories() (existing seam)
host/BotSessionAdapter   — CreateHeadlessSession + NullSessionAnticheat + reclaim

runtime/BotManager       — map<ObjectGuid, BotEntry>
                           BotEntry { BotRecord (lifecycle) + BotController* + Player* cached }

runtime/BotController    — ONE per bot. Small, explicit state machine (see §7).
                           Owns:
                             Intent         persistent = Follow{masterGuid, angle, formation="near"}
                                                      | Stay{anchor: WorldLocation}
                             CombatState    transient = None | Active{targetGuid, phase}
                             PerceptionView cached perception snapshot (refreshed lazily)

behavior/Movement        — pure helpers around MotionMaster
                           Follow(master, followDistance, angle)
                             → MotionMaster::MoveFollow(master, dist, angle) if useful
                             → StopMoving() if already at target
                           Chase(target, meleeReach)
                             → MotionMaster::MoveChase(target, 0, 0)
                           Hold()
                             → MotionMaster::Clear() + UNIT_STAT_CHASE/FOLLOW clear
                           ReturnToAnchor(anchor)

behavior/Targeting       — pure predicate
                           IsValidHostileForAssist(bot, unit, sightDistance=75.f) → bool
                             (IsInWorld, same map, alive, !IsFriendlyTo, CanAttack,
                              !UNIT_FLAG_NOT_ATTACKABLE_1/UNTARGETABLE, distance ≤75,
                              IsWithinLOS where feasible, immune/thorns gate)
                           AssistOwner(bot) → Unit* | nullptr
                             masterSel = owner->GetSelectionGuid() → GetUnit()
                             if masterSel && IsValidHostileForAssist(bot, masterSel) → masterSel
                             else if masterSel empty && master victim exists and valid → master victim
                             else nullptr
                           TargetDirty(current, candidate) predicate for re-attack

behavior/Combat          — thin verbs
                           Start(bot, target)  // mirrors cmangos AttackAction::Attack ordering
                           Stop(bot)           // mirrors SelectNewTargetAction cleanup
                           TickApproach(bot, target, range) // enemy out of melee guard

commands/BotCommands     — .bot follow|stay|attack|stop|remove
                           Each builds an *intent*, not a motion. Returns "ok" and
                           controller picks it up on next OnWorldUpdate.

perception/             — small read helpers (no scans per tick in MVP)
                           OwnerState          { ObjectGuid selection, bool isAlive, bool isFlying, Map* }
                           CombatSensors       { Unit* current, bool isAlive, bool isInSameMap }
                           VisibleCache        { lazily filled Guid→Unit* for target+attacked set }
```text

### Responsibilities

| Piece | Owns | Does NOT own |
| --- | --- | --- |
| `BotManager` | lifetime (`AddBot/RemoveBot` → headless session + `BotController`), tick loop `for (entry: bots) entry.controller->Update(diff, worldPosSnapshot)` | AI, movement, targeting — delegates |
| `BotController` | persistent intent, transient combat state, decision to `Follow`/`Chase`/`Attack`/`Stop` and to remember `previousIntent` for return | `MotionMaster` internals, DB, packets |
| `Movement` | single-motion selection, useful-guard, `StopMoving` coalescing | which target to pick |
| `Targeting` | pure `IsValid` + `AssistOwner` | movement |
| `Combat` | `SetSelectionGuid`, `SetFacingTo`, `Bot::Attack/AttackStop`, loot push | follow distance |
| `Commands` | parse `.bot` syntax, validate ownership/security via `PlayerbotSecurity`-like ladder (same-account or guild+config), call `controller->SetIntent` | execute motion |

**Why not the current proposed `BotManager|BotContext|BotOwnership` from `docs/PLAN.md §7`?** `BotContext` plus `Perception::*` plus `Strategy` triplication is premature. For the vertical slice, `BotController` plus three pure helper namespaces (`Movement`, `Targeting`, `Combat`) replaces each of those layers with less indirection and no string dispatch. `BotOwnership` belongs inside `BotManager` + a tiny security predicate until LFG/guild-fill arrives (Phase 5).

**Formation handling:** one enum now (`NearFollow` = keep moving `1.5y` behind master at sampled angle; future: `Behind`/`Melee`/`Queue`/`Circle` are just alternative `angle`/`offset` suppliers — introduce `Formation::GetLocation()` interface only when Phase 5's 2-humans+3-bots party actually needs it.

---

## 7. State transitions (MVP)

### Persistent intent (one word of storage)

```text
Intent = Follow{masterGuid, followAngle}   default after AddBot
       | Stay{anchor: WorldLocation}

Commands:
  .bot follow   → Intent = Follow{masterGuid=masters GUID, angle=sampled once}
  .bot stay     → Intent = Stay{anchor = bot's current WorldPosition}
  .bot remove   → lifecycle = Removing (handled by BotManager, not controller)

No other persistent intents in MVP. Guard/wander/travel/wait are not persistent —
they are transient CombatState/TravelTarget and are omitted.
```text

### Transient combat (depth-1 suspend/resume)

```text
CombatState = None
            | Active{targetGuid, phase: Approaching | Attacking}

Entry:  on tick, if CombatState==None and Targeting::AssistOwner(bot) → Some(unit)
        → CombatState = Active{unit, Approaching}
           save previousIntent = Intent   (depth 1 only, no stack)
           Movement::Hold() any current follow point

Phase:  if distance2d(bot, target) > meleeReach (+ contactDistance 0.5y)
           Movement::Chase(target)          // enemy out of melee
        else
           if !IsInFront(CAST_ANGLE_IN_FRONT) → SetFacingTo(target)
           Combat::Start(bot, target)       // AttackAction ordering
           phase = Attacking

Stay:   CombatState==Active while Intent==Stay
        → bot *may* leave the anchor to fight, but only within reactDistance (150y)
          and only if IsValidHostileForAssist. Pet Stay is deferred.

Exit:   InvalidTargetValue(target) == true
           (target gone / dead / wrong map / not attackable / out of 75y for >N ticks)
        OR target Units dead flag
        OR owner cleared selection AND no valid AssistOwner for ≥1 s (hysteresis)
        → Combat::Stop(bot)   // AttackStop, clear selection, pet COMMAND_FOLLOW
           CombatState = None
           restore Intent = previousIntent
             if previousIntent was Stay → Movement::ReturnToAnchor(anchor)
             if previousIntent was Follow → Movement::Follow(master) resumes next tick
```text

### Visualized

```text
AddBot
  └→ Intent=Follow(master) ── Has AssistOwner? ──┐ no
        │ tick                                Follow
        │   Has AssistOwner?                  tick loop
        ├─ yes ─→ CombatState=Active ─────────────────┐
        │          save prev=Follow, Approach/Chase     │
        │          invalid/dead?                        │
        └─ no ──── CombatState=None ←── CombatState=Active
                  resume Follow              Stop → Follow
                                          (previousIntent)

.bot stay
  └→ Intent=Stay(anchor=posNow), prev stays implicitly saved when combat suspends it
     Has AssistOwner? ─ yes ─→ CombatState=Active, leave anchor up to ≤reactDistance
                      ─ no ─→ Hold until command or combat entry
     CombatState exits → ReturnToAnchor → Hold

.bot follow
  └→ Intent=Follow (clears anchor, also clears Reaction/Wait state)

Death / map leave
  └→ CombatState=None, Hold(), Intent unchanged (bot stays dead anchor until res)
     DEAD handling (corpse run / spirit healer) deferred to dungeon MVP

Remove
  └→ Entry removed in BotManager (pending-cancel if queued, else LogoutPlayer)
```text

### Is this sufficient?

Yes for the single human + melee dungeon bot slice. Explicitly **not** sufficient for: healer target switching, tank taunt cycle, multi-mob AoE, CC, wipe recovery, random travel. Those are the *payload* of `DpsAssistStrategy` + `AttackersValue` + `PossibleAttackTargetsValue` + `ThreatManager` scanning, which is exactly the surface to postpone to Phase 5. The MVP's one-predicate assist plus return-to-previous-intent already covers the 13-point journey the task lists (follow/stay/resume/attack/approach/die/stop/resume/remove).

---

## 8. Update loop — recommended cadence and ordering

### Cadence (source-grounded)

| Bot state | Period | Source analogue | Notes |
| --- | --- | --- | --- |
| **Combat (`CombatState==Active`)** | 100–150 ms | `reactDelay=100ms` + `globalCoolDown=500ms` but combat wakes to `reactDelay` (`PlayerbotAI::UpdateAI` wake resets `aiInternalUpdateDelay`) | Must be tight enough that melee approach feels alive but not 20 Hz. Legacy `UpdateAIReaction(minimal=false)` is 100 ms; `FaceTargetUpdateDelay = reactDelay*5` in combat minimal. Use 120 ms as default. |
| **Follow (non-combat, near master)** | 200–250 ms | `reactDelay 100ms` normal tick; `FollowAction::isUseful` checked every non-combat trigger sweep (`FollowMasterStrategy::InitNonCombatTriggers`) | 4–5 Hz is plenty — movement is `MoveFollow` (auto-tracks), so ticks just guard `isUseful` and target changes. |
| **Stay / idle-follow (within 1.5 y)** | 1000–2000 ms | `passiveDelay=4000ms` minimal, `PassiveMultiplier` / `AllowActivity` gating | Reduces tick CPU linearly with bot count; Tortoise target is "a few owned dungeon bots" (PLAN §14), but do not design a 1000-bot idle cost into the MVP. |
| **Perception cache expiry** | `InvalidTargetValue::checkInterval = 1` tick; `CalculatedValue` uses `lastCheckTime` + `checkInterval/2` guard | Legacy `LazyCalculatedValue` caches by wall clock; many values already gated by `AllowActivity` | For MVP: `IsValidHostileForAssist` revalidates `master->GetSelectionGuid()` every non-combat tick (it's one deref), but cached `attackers` scan — if any — is at most once per second and only while in combat. |

### Ordering per `BotController::Update(diff)` (inside `BotManager::OnWorldUpdate`)

```text
0. Early outs (no allocation, no scan):
     if bot->IsBeingTeleported() or !IsInWorld() or IsTaxiFlying()/on transport
        or !CanMove() (stunned/support flags) → Hold() + schedule next check + return
     if nextCheckDelay > diff → subtract and return   // throttling — mirrors aiInternalUpdateDelay

1. Snapshot (O(1), no grid):
     owner = master Player* from BotEntry.masterGuid (stable until RemoveBot)
     cur   = combat.targetGuid → GetUnit(cur) if any
     assist= Targeting::AssistOwner(bot)   // 1× GetSelectionGuid + 1× GetUnit + IsValid

2. Target lifecycle:
     if cur && InvalidTargetValue(cur) → Combat::Stop, CombatState=None → goto 4
     if !cur && assist               → CombatState=Active{assist, Approaching}, save prev Intent

3. Combat phase:
     if CombatState==Active
        if cur alive && same map && IsValid(cur)
           if EnemyOutOfMelee(bot, cur) → Movement::Chase(cur)
           else → Movement::Hold() if chasing, Combat::Start(cur) (which does AttackEnable)
        else → Combat::Stop → fall through

4. Persistent intent (only when CombatState==None):
     match Intent
       Follow { masterGuid, angle }:
          if distance2d(bot, master) > followDistance+contactDistance (1.5+0.5)
             Movement::Follow(master, followDistance, angle)
          else if Movement is FOLLOW_MOTION_TYPE and target==master and angle==savedAngle → no-op
          else Movement::Hold()
          // map change / far teleport (>sightDistance*3): BotManager-level shortcut
          // TeleportTo(master.pos) rather than long navmesh, only when not in combat

       Stay { anchor }:
          if distance2d(bot, anchor) > contactDistance (0.5y)
             Movement::ReturnToAnchor(anchor) → MovePoint(anchor)
          else Movement::Hold()

5. Housekeeping (every Nth tick, not every diff):
     Every ~5 s: update master guid shadow (group leader change / owner relog)
                 — mirrors legacy "guid-shadow revalidated every tick" but batched
     Every ~10 s sampled: decision ring-buffer row (intent, target, motion, reason)
     Never per-tick: DB, aura iteration > small set, threat copy, packet queue drain

6. Schedule next check (mirrors SetAIInternalUpdateDelay / YieldAIInternalThread):
     if CombatState==Active → nextCheck = reactDelay (100–150 ms)
     else if was following and moved → nextCheck = reactDelay
     else → nextCheck = 800–1500 ms (stay/idle drift)

Total per-tick cost per idle bot: 1×GetUnit + 1×distance check + 1×motion-type check.
No grid search. No DB. No trigger list traversal.
```text

### Expensive ops to avoid explicitly

* `Cell::VisitAllObjects(player, searcher, range)` every tick (`PossibleTargetsValue::FindPossibleTargets`) — restrict to an **explicit** `ScanAttackers` call only when entering combat, not as polling.
* `AttackersValue::IsValid` sharing via nearby bot copies — deferred.
* `ValueContext::Update()` per tick (was 8% idle) — remove entirely; explicit invalidation on target change is enough.
* `LootObjectStack` scan per tick — fill only when `invalid target` due to death was true, not eagerly.
* Any `CharacterDatabase` access inside `OnWorldUpdate` — prohibited by `AGENTS.md` perf rules.

### Correctness vs cost trade

The MVP intentionally sacrifices "attack master's attacker before selection changes" (which would need a bounded `VisitAllObjects` scan) for "attack owner's explicit selection". That scan is the exact cost center MangosZero fixed on 2026-08-20 ("`NearestGameObjects` 20/sec idle" → per-tick fix). Bring the scan back in Phase 5 when tanking and AoE actually need it.

---

## 9. First implementation plan — exact order, vertical so every step demos in-game

Each slice must leave `BUILD_PLAYERBOTS=OFF` green and an observable in-game check.

### Slice 0 — Stubs (no gameplay yet, unblocks compilation) — 0.5 day

* Files: `runtime/BotController.h/.cpp` (empty shell), `behavior/Movement.h/.cpp` (no-ops), `behavior/Targeting.h/.cpp` (false predicate), `behavior/Combat.h/.cpp` (no-ops), `commands/BotCommands.h/.cpp` (help only), `perception/OwnerState.h` (trivial).
* Wire `BotManager::OnWorldUpdate` to iterate bots and call `controller->Update(diff)` (throttled to 1 s idle to prove tick wiring); log once per 10 s with `BotController active`.
* Test: login human, `.bot add Dudette` → bot still follows by existing? no behavior change; tick log appears; `BUILD_PLAYERBOTS=OFF` builds clean.

### Slice 1 — Follow (visible win) — 1 day

* Implement `Movement::Follow(master, 1.5f, angle)` with the public-target/moving-state `FollowAction::isUseful` guard + `MotionMaster::MoveFollow`/`MovePoint` fallback, and `BotController` intent `Follow` default.
* `IsInWorld` / `IsBeingTeleported` / taxi / `CanMove` early-outs.
* Test: `.bot add` → bot follows at ~1.5 y. Owner runs continuously → bot tracks without jitter. Owner stops → bot stops within a step. Cross-map teleport (e.g. `.go xyz`) with bot on same map far → bot uses `MoveFollow` (or `TeleportTo` shortcut if `>150y` and not in combat). No flying/path special cases.

### Slice 2 — Stay / resume — 0.5 day

* `SetIntent(Stay{anchor})` snapshots anchor; combat entry suspended to §7. `.bot follow` clears anchor.
* Implement `ReturnToAnchor` via `MovePoint`.
* Test: `.bot stay` → bot holds anchor, owner walks away → bot holds. Owner does `.bot follow` → resumes follow. While Stay, owner enters combat with no target → Stay still holds. (Combat-resume tested after Slice 4.)

### Slice 3 — Target validation predicate — 0.5 day

* Implement `IsValidHostileForAssist` (hostile, alive, same map, `CanAttack`, within `sightDistance 75y`, flag checks, LOS where feasible, thorns/reflect immunity gate stub returning false for `HasAuraType(SPELL_AURA_DAMAGE_SHIELD)` >10% threshold per `e9e7009`).
* Implement `AssistOwner` reading `master->GetSelectionGuid()` + `sObjectAccessor.GetUnit(bot, guid)`.
* Test unit-style: create synthetic target scenarios (friendly, dead, far, untargetable, shielded) — predicate returns expected. No bot motion yet.

### Slice 4 — Assist → Attack (melee approach + auto-attack) — 1 day

* Wire `CombatState` suspend/resume with `prevIntent`.
* Implement `Combat::Start` ordering (selection, oldTarget save, pet attack, facing, `Attack` call) and `Combat::Stop` ordering (loot push, `AttackStop`, pet follow, clear selection).
* Implement `enemy out of melee` → `MoveChase` approach tick (reuses `MeleeCombatStrategy` predicate: `distance > meleeReach + contactDistance`).
* Test per the journey:
  * `.bot follow` → master attacks mob (targets it) → bot assists, chases if out of melee, starts auto-attack, keeps selection.
  * Master switches target mid-fight → bot re-validates and re-tackles new `AssistOwner` target (invalidate old, `Stop` + new `Start`).
  * Master clears target → after ~1 s hysteresis bot stops attack, clears selection, resumes follow without needing a `follow` re-command.
  * Target dies (`UnitIsDead`) → bot pushes corpse to `available loot` placeholder, `AttackStop`, resumes follow.
  * Target unreachable (LOS/flags) → `IsValid` false → `Stop`.
  * Bot already had a different target → `AssistOwner` wins; `Stop` previous before `Start` new.

### Slice 5 — Polish for the 13-point journey + removal — 0.5 day

* Facing while attacking (`SetFacingTo` when `!IsInFront(CAST_ANGLE_IN_FRONT)`), keep `isUseful` avoid spam, `Stop` on `invalid target` (target GUID != `bot->GetSelectionGuid()` per `InvalidTargetValue` rule).
* Wiring `.bot attack [name|guid]` as "direct attack" (bypasses assist, but still validated) and `.bot stop` / `.bot remove` → lifecycle `Removing` + queued-cancel handling already proven in Phase 3.
* Test full journey end-to-end for 30 minutes: login → add → follow → stay → follow → attack mob → mob dies → resume → remove → relog human → no session leak, `characters.online/account.online` zero after shutdown (Phase 3 regressions re-verified).

**Total: ~4–5 days of focused slices.** Do not add class rotations, heal/dispel, ranged, gossip/quest, loot rules, random bots, or travel in this window.

---

## 10. Acceptance test matrix — deterministic runtime/manual tests

Run on `tortoise-docker-penqle` sibling stack (per `AGENTS.md`); use the existing fixture `account 4 / character Dudette guid 1` (already proven reclamation) or a fresh `AutoTest` pair.

| # | Title | Steps | Expected | Proven failure if broken |
| --- | --- | --- | --- | --- |
| A1 | **Follow dead-zone** | `AddBot` (Dudette) → owner stands still within 1.2 y | Bot holds, no `MoveFollow` re-issued, no jitter | Re-issuing every tick causes motion stutter and 10× motion packets |
| A2 | **Follow while owner runs** | Owner runs 60 y in straight line on flat terrain | Bot tracks at ~1.5 y behind, never gaps > `sightDistance`, never overshoots LOS | Stale cached angle/path (bug `b6f31474` pre-fix) |
| A3 | **Follow angle stability** | 2 successive ticks while follow target steady | No new `MoveFollow` when chase target/angle/offset unchanged | Missing `isUseful` guard (Shyalya jitter) |
| A4 | **Stay holds** | `stay` → owner walks 40 y away → wait 10 s | Bot stays at anchor (±0.5 y), no follow triggered | Stay is incorrectly a non-combat-only strategy |
| A5 | **Stay resumes via follow** | A4 → `follow` | Bot resumes `MoveFollow` to master next tick | Missing `PositionMap["return"]` reset |
| A6 | **Assist adopts owner's target** | `follow` → master `/target EnemyA` (valid hostile within 75 y), owner auto-attacks | Bot within ~200 ms sets `current target=EnemyA`, faces, `Attack(EnemyA)` | Using `attackers` scan instead of selection loses intent |
| A7 | **Target validation — friendly** | Master targets friendly NPC | Bot does NOT attack | Missing `IsFriendlyTo` check |
| A8 | **Target validation — dead** | Kill EnemyA → while corpse selected | Bot does NOT attack corpse (dead gate) | Missing `UnitIsDead` |
| A9 | **Target validation — untargetable** | Master's target has `UNIT_FLAG_NOT_ATTACKABLE_1` | Bot does NOT attack | Missing flag check |
| A10 | **Target switch** | Bot attacking EnemyA → master switches to EnemyB | Bot `AttackStop` EnemyA, `Start` EnemyB within one throttle period | No `prevTarget != AssistOwner` re-validation |
| A11 | **Master clears target** | Bot attacking → master `cleartarget` (ESC) | Bot `AttackStop` after hysteresis (~1 s), resumes `Follow` | Combat leaked — classic stuck-attacking bug |
| A12 | **Target dies** | Bot attacking → mob HP→0 via owner | Bot `AttackStop`, corpse GUID staged for loot (no auto-loot needed), next tick resumes `Follow` | Missing death path |
| A13 | **Approach if out of melee** | `follow` → `AssistOwner` target at 18 y | Bot `MoveChase` until `≤ ATTACK_DISTANCE`, then `Attack` | Never chasing — melee range bug (`231e05c8` pre-fix) |
| A14 | **Approach not for stay?** | `stay` at anchor → valid `AssistOwner` 30 y away | Bot *does* leave anchor to chase (Stay suspends, within `reactDistance 150y`) and on `Stop` returns to anchor, not to master | Stay incorrectly absolute |
| A15 | **Combat blocks redundant follow** | Bot `CombatState==Active` → owner moves away | Bot chases target, not master; after `Stop` resumes master follow | Follow stealing chase |
| A16 | **Bot removed cleanly** | `CombatState==None, Intent==Follow` → `RemoveBot(guid)` | Pending `AddSession` cancelled if queued, else `LogoutPlayer` → `online=0/0`, `BotRecord` erased, no `AddBot→RemoveBot` dangling (regression `PendingAddRemoveTest PASSED`) | Phase 3 regression |
| A17 | **Human reclaim** | Headless Dudette `InWorld` (`online=1`) → real Network session same acct/guid `CMSG_AUTH_SESSION` + `CMSG_PLAYER_LOGIN 1` | `World::AddSession_` moves old to `m_disconnectedSessions` via `ForcePlayerLogoutDelay`, `BotManager` auto-releases to `!IsHeadless() or !isOurAccount` → no duplicate `Player`/`WorldSession`,`SMSG_LOGIN_VERIFY_WORLD 0x236` | Duplicate session leak |
| A18 | **Shutdown with active bot** | `follow` + combat → `docker compose stop mangosd` | `characters.online=0`, `account.online=0`, `World::InternalShutdown` drains queue cleanly | Leaked queued session |
| A19 | **Far teleport shortcut** (optional) | Owner on other continent (>150 y) not in combat → tick | Bot uses `TeleportTo(masterPos)` once, not a 10 km navmesh path | Long-path miss |

All tests are manual in-game with console/mangosd log + SQL checks (`SELECT online FROM characters`, `account.online`) — no ad-hoc harness that fabricates travel graph.

---

## 11. Open questions — only what needs runtime implementation to decide

1. **Should `AttackStop` also clear `PositionMap["follow"]`/`"stay"`?** Legacy `StayChatShortcutAction` did `MEM_AI_VALUE(WorldPosition,"master position")->Reset()` on every strategy change to avoid stale `master position` carrying after teleport. Whether the same stale-carry affects our simple `anchor` after cross-map teleports needs a live observation; otherwise keep `anchor` stable after teleport.

2. **Is 1.5 y follow distance right for Tauren/colossus or should it be `max(1.5, bot->GetObjectBoundingRadius()+target->GetObjectBoundingRadius()+contactDistance)`?** CMaNGOS adds `GetObjectBoundingRadius` in `MoveNear` (`mangoszero MovementActions.cpp:40`); Vanilla 1.12 used a looser dead-zone. The minimal 1.5 y is correct per source but may collide with large models — confirm in Turtle 1.18.1 with a Tauren bot + Tauren master in Orgrimmar doorway.

3. **Should `IsValidHostileForAssist` also check `IsWithinLOSInMap` on the bot side?** `AttackersValue` checks `IsWithinLOSInMap` + `GetDistance2d`; `PossibleTargetsValue` checks `IsWithinLOS(x,y,z)` candidate point. Adding a per-tick LOS raycast is a perf hit. Try spec-first LOS (always treat as pass) and only add the bot LOS check if we observe bots attacking through walls in a real dungeon doorway.

4. **Exact `Attack(target, melee)` flag semantics in Turtle 1.18.1.** In MaNGOS-Zero `Attack(target, true)` arms melee swing; with `false` it only sets `victim` for ranged. Confirm via `Player::Attack` in the local `tortoise-docker-penqle/source` tree (check `Unit::Attack` signature arity and whether a ranged bot in the future needs `false`). MVP is melee so `true` is correct, but record before adding hunters.

5. **Collection of replicas for `followDistance`:** AzerothCore world is 3.3.5 with different movement; the 1.5 y constant predates modern pathfinding improvements. Re-measure actual `MoveFollow` visual gap with two live clients (owner walking vs running) to confirm the dead-zone doesn't read as "stacked" — might tune to `2.0–2.5y` without breaking legacy semantics.

---

## 12. Provenance

| Feature / evidence | Repository | Commit | Files | Copied / reimplemented | Reason / note |
| --- | --- | --- | --- | --- | --- |
| Overall design posture (harvest behavior not arch, module ≤5 host files, headless = transport) | `AGENTS.md` / `docs/PLAN.md` / `docs/HOST_API.md` | TortoiseBots `0797fce` (this repo HEAD) | `AGENTS.md:1–300`, `docs/PLAN.md:1–400`, `docs/HOST_API.md:§0–§10` | Reimplemented — existing Phase 3 seam reused as-is; this discovery proposes no new core hook beyond `IWorldUpdateListener`+`SessionTransport` | Phase 3 proven: `0797fce` human reclaim + queued lifecycle |
| Follow distance 1.5 y / `sightDistance 75.f`, `reactDistance 150.f`, `contactDistance 0.5f`, tick delays | `cmangos-playerbots` | `076045e` (`2026-08-16 Add misdirection…`) | `playerbot/PlayerbotAIConfig.cpp:122`sightDistance`, :126`reactDistance`, :138–145`followDistance/contactDistance`,`PlayerbotAI.cpp:580 reactDelay`,`Strategy/Engine.cpp:140 iterationsPerTick` | Reimplemented as constants + throttle; not copied | Vanilla-stable defaults; MangosZero equivalent `PlayerbotAIConfig.cpp:149 followDistance 1.5f` |
| `FollowMasterStrategy` triggers (`out of free move range` → follow, `update follow` idle) | `cmangos-playerbots` | `076045e` | `strategy/generic/FollowMasterStrategy.cpp:7–13`, `.h` | Reimplemented as explicit `Intent Follow` + `isUseful` guard, not as trigger registration | Trigger idea preserved, registration debt dropped |
| `FollowAction::Execute` + `isUseful` + `CanDeadFollow` + `GetFollowAngle` | `cmangos-playerbots` | `076045e` | `strategy/actions/FollowActions.cpp:13–118`, `strategy/values/Formations.cpp:59 GetLocation`, `Formations.h` | Ported predicate logic; action class not copied | Validates follow semantics (formation offset/angle, rpg-target veto, chase guard) |
| `MovementActions::MoveTo2/FlyDirect/DispatchMovement/UpdateFlyingState` (travel complexity) | `cmangos-playerbots` | `076045e` | `strategy/actions/MovementActions.cpp:33–800` | **Not ported** — documented as debt for random bots only | Would add 2000 lines for owned-bots benefit |
| MangosZero simpler follow (`Follow(Unit*,dist,angle)` → `MoveFollow`) | `mangoszero-server` | `1817ae1` | `src/modules/Bots/playerbot/strategy/actions/MovementActions.cpp:443–585`, `PlayerbotAIBase.cpp:351`, `ChatShortcutActions.cpp:1–70` | Preferred pattern for MVP (adopted) | Demonstrates cleaner layering for owned bots |
| Stay (`StayActionBase::Stay`, `StayStrategy`, `return to stay position`) | `cmangos-playerbots` | `076045e` | `strategy/actions/StayActions.cpp:14–48`, `strategy/generic/StayStrategy.cpp:1–30` | Reimplemented as `Intent Stay{anchor}`; strategy-trigger not copied | Preserves anchor + return-after-combat idea |
| Stay/Follow chat toggles (`FollowChatShortcutAction`, `StayChatShortcutAction`) | `cmangos-playerbots` (`Stay` + `Follow`) | `076045e` | `strategy/actions/ChatShortcutActions.cpp:35–110` | Reimplemented as `controller.SetIntent` | Converts string strategy toggle to typed intent |
| MangosZero stay toggle (`+stay,-passive` vs `+follow master,-passive`) | `mangoszero-server` | `1817ae1` | `strategy/actions/ChatShortcutActions.cpp:28–70` | Reimplemented similarly | Evidence of minimal intent toggle |
| Combat order (`AttackAction::Attack` / `IsTargetValid` shield check, pet `COMMAND_ATTACK`) | `cmangos-playerbots` | `076045e` (also fix `e9e7009` thorns, `993bfc5c` immunity) | `strategy/actions/AttackAction.cpp:80–230`, `MeleeCombatStrategy.cpp:1–25` | Port verb | 10% max-HP shield guard + selection/facing/pet order matters |
| Target selection (`DpsTargetValue`, `FindLeastHp`, `FollowTargetValue`→`master target`) | `cmangos-playerbots` | `076045e` | `strategy/values/DpsTargetValue.cpp:9–18`, `strategy/values/TargetValue.cpp:130 FollowTargetValue`, `strategy/values/AttackersValue.cpp:30–120` | Reimplemented as `AssistOwner` predicate | Preserves "assist master" priority without 195 values |
| Target validity (`PossibleAttackTargetsValue::IsValid`, `PossibleTargetsValue::IsValid`, `InvalidTargetValue`) | `cmangos-playerbots` | `076045e` (fixes `998c521e` all-school immunity, `c32d4fb4`, `347d9acb`) | `strategy/values/PossibleAttackTargetsValue.cpp:313`, `strategy/values/PossibleTargetsValue.cpp:90`, `strategy/values/InvalidTargetValue.cpp:13` | Reimplemented as single `IsValidHostileForAssist` | Core of "does not attack invalid" guarantees |
| `SelectNewTargetAction` cleanup (loot, oldTarget, AttackStop, pet follow) | `cmangos-playerbots` | `076045e` | `strategy/actions/ChooseTargetActions.cpp:130–220` | Port verb | Defines correct stop ordering |
| Engine / Trigger / Value debt (`Engine::DoNextAction`, `ProcessTriggers`, `ValueContext`) | `cmangos-playerbots` | `076045e` | `strategy/Engine.cpp:123 DoNextAction`, `Strategy.h:24 StrategyType`, `strategy/Value.h`, `ValueContext.h:305 follow target` | **Debt** — not ported; documented as avoid | Justification for explicit controller |
| World tick + headless seam reuse | `Penqle/tortoise-wow` via local `tortoise-docker-penqle/source` + `HOST_API.md` §10 | source `d07ec3f` / `HOST_API.md` Phase 3 | `src/game/World.cpp:2448 Update`, `WorldSession.h:HasNetworkTransport`, `host/BotHostAdapter.cpp`, `runtime/BotManager.cpp` | Reuse verbatim — no new hook | Already proven |
| Turtle compatibility evidence (compat shim, `NullSessionAnticheat`, ~80 host hooks, talent fixes) | `shyalya-tortoise-wow` | `1f9497e` (vendored `c33dfac` via `0af2567 2026-05-10`) | `src/modules/PlayerBots/cmangos-compat-shim.h:1–80`, `Anticheat/Anticheat.h:143` precedent (`shyalya` hooks) | Compat names noted, host hooks rejected | Why MVP stays minimal — shim shows scale of naive port |
| Capability / command baseline (`.bot add/login/remove`, 137 chat triggers, security `ALLOW_ALL`) | `TortoiseWoWKnowledgeBase` | `172ee948` (`2026-08-12`) | `playerbots/console-commands.md`, `chat-surface.md`, `security-failures.md`, `capability-map.md` | Behavioral oracle — implemented as intents | Defines public contract vs internals |
| Recent 87-commit delta (`Rework CC`, `thorns/reflect`, `War Stomp`, `too close to cast`, `GCD reactDelay`, `flight back-and-forth`) | `cmangos-playerbots` | `c33dfac..076045e` | Full range listed in DISCOVERY §7 + `80e897d8`, `e9e7009`, `b4f61487`, `0ddc94eb`, `d5d48054`, `9f2776f8` | Behavior adopted where relevant | Shows which fixes actually matter for 1.18.1 |

Upstream licenses (GPL-2.0 for MaNGOS/CMaNGOS/Shyalya) preserved; no large code bodies were copied for this discovery doc.

---

## Per-feature drill-downs (as requested)

### Feature A — Bot command / control model

#### Observable behavior

Human types `.bot add Dudette` → module replies `ok` and Dudette appears in world within one world tick, already following. `.bot follow` resumes following after a stay; `.bot stay` anchors the bot where it stands (but it may still leave the anchor to fight and return afterward); `.bot attack [unit]` forces assist on that unit; `.bot stop` drops combat but not the persistent intent. `.bot remove` queues or immediately logs the bot out and frees the headless session without leaving `online=1`.

#### Knowledge Base

`playerbots/console-commands.md` — target grammar (`*`, `guild`, `!` rank>4, comma-separated names), `command=subtype` param, per-target `<command>: <bot> - <result>` shape, async `add/login` returning `ok` before world entry, ownership `Not in your guild or account`, `security-failures.md` ladder `DENY_ALL(0)..ALLOW_ALL(4)` with `INVITE` first gate then `ALLOW_ALL`, and `TALK` fallback (`I'm kind of busy now`). `chat-surface.md` documents the 137-key prefix parser that must **not** be reproduced as a registry.

#### CMaNGOS PlayerBots

*Files:* `PlayerbotAI.cpp:254 UpdateAI` (security whisper handling), `PlayerbotMgr.cpp` (command dispatch + `SecurityLevelFor`), `ChatShortcutActions.cpp` (follow/stay intent toggles), `strategy/ChatActionContext.h` (123 cmd aliases). *Classes:* `PlayerbotAI`, `PlayerbotHolder/PlayerbotMgr/PlayerbotSecurity`. *State:* `master` pointer + guid shadow, `security` object. *Timers:* reply queue delay 10–20 s out-of-combat. *Edges:* offline `always`, random prefix `rndbot pid`, `queue <cmd>` whisper-excluded, disallowed if owner teleporting. *History:* `b7ddbdb8` log analysis crash fix, `d875bb0f` default help.

#### Shyalya

Turtle adds `AllowGuildBots`, `AllowMultiAccountAltBots`, `NullSessionAnticheat` for bot sessions, fixes `self login` persistence. No change to the trigger-key registry — confirms the registry belongs outside the core seam.

#### MangosZero

`ChatShortcutActions.cpp:28 Follow "+follow master,-passive"` vs `Stay "+stay,-passive"` — evidence that two intents per `BOT_STATE_*` is enough; no `wander`/`guard` split. Simpler queue-less tells.

#### Transferable

Same-account ownership default; `add`→ queued login with `ok` before world entry (don't block the caller's thread); follow/stay as toggled intents rather than ad-hoc motion.

#### Legacy baggage

137-key prefix table, `CommandSeparator "\\"` stacked commands, command-recording capture, `@` filter injection into movement decisions.

#### Recommended design

`.bot add/remove` in `commands/BotCommands.cpp` via `ChatHandler` registration (or single `WorldSessionScript::OnPacket` filter). Security predicate `CanControl(requester, botEntry)` mirrors `PlayerbotSecurity::LevelFor` read as `ALLOW_ALL if requesterGuid==ownerGuid || isInGroupOfBot && sameMap` — defer guild/rank/gear gate to Phase 5. Command builds an intent (`Follow`/`Stay`/`Attack(guid)`), controller consumes it next tick. Reply via `WorldSession::SendPacket` chat line only after bot verifies target.

#### Acceptance tests

A4 (stay holds), A5 (follow resumes), A16 (remove clean), A17 (reclaim), and a new ownership probe: second account attempting `.bot add Dudette` receives `Not in your guild or account`.

---

### Feature B — Bot update loop

See §8 for full cadence. Summary: **one world-tick driver** (`IWorldUpdateListener`), one per-bot throttled `BotController::Update(diff)`, one `nextCheckDelay` counter, no secondary thread touching `Player`. Expensive helpers (`CanAttack`, flag checks) cached at call sites, not in a shared `ValueContext::Update`.

Recommends **120 ms combat / 200 ms follow / 1000 ms idle** as defaults mirroring the measured 100 ms/4000 ms split in `PlayerbotAIConfig.h:112–113` but halved for modern tick budgets (observed settled tick ~27 ms for 50 bots at `reactDelay 100` on Shyalya reference deployment, `playerbots/performance.md`).

*Expensive to avoid:* per-tick `Cell::VisitAllObjects` (see §8), per-tick `ValueContext::Update()` (8% idle no-op, disabled in legacy), per-action `sPerformanceMonitor.start` inside `Engine::DoNextAction`.

---

### Feature C — Follow

See §2 row + §4.1 + §6 `Movement::Follow`. MVP movement is bounded by:

* `Master walking ⇒ slave walks` (`WalkDistance` check, `MovementActions.cpp:900 masterWalking` branch).
* Teleport / map-difference: direct shortcut `TeleportTo(masterPos)` instead of building `TravelNode` path when `startMap != endMap` or `distance > sightDistance*3` (legacy fallback in `MovementActions.cpp:412`).

*Stuck/pathing for MVP:* only `IsPushed` trampoline (`IsJumping`/`HasFlag UNIT_FLAG_FLEEING/CONFUSED`) and `UpdateAllowedPositionZ` before `MoveFollow` are needed. Full `FleeManager`/unstuck/dungeon regroup deferred to Phase 5 — the dungeon MVP path is enough to expose a real stuck repro before building a generic fix.

*Acceptance:* A1–A3 + Phase 3 teleport leave-world/reenter.

### Feature D — Stay

See §2, §4.2, §7. Simpler than either reference:

* `Stay == Hold anchor` and the *only* non-idempotent piece is the anchor snapshot. MVP should send `silent` stay (legacy `StayActionBase::Stay` suppresses tell unless `verbose`), and remain rotation-capable — `SetFacingTo(target)` is a combat action, not a movement one, so a stayed bot may still face its attacker before `Chase`.

*Acceptance:* A4–A5, A14. Extra probe: while stayed, clearing `AssistOwner` does not auto-clear anchor — explicit `follow` required (intent ≠ transient).

### Feature E — Assist / target selection

See §2, §4.3. Priority chain (owned DPS) documented in §4.3, validated by `DpsTargetValue` and `RtiTargetValue`. Validation gates (hostile check, dead check, flag check, range, LOS where feasible) come from `PossibleTargetsValue` and are listed with fix SHAs in §12.

*Priority nuance:* legacy had no explicit "owner's attacker" value — group assist came from `AttackersValue` (threat-derived). For MVP's single-target slice, the owner's explicit selection is the correct contract; adding "owner's attacker" would require a bounded `AnyUnfriendlyUnitInObjectRangeCheck` around the **owner**, not the bot, which is both cheaper and more intentful — defer to dungeon MVP.

*Acceptance:* A6–A12, target-invalid probe, switch probe.

### Feature F — Basic combat / auto attack

See §2, §4.4, §6 `Combat` verbs, §7 exits. Start ordering preserves `AttackAction::Attack` which already encoded recovered bugs (mount, shield, pet `COMMAND_ATTACK`, `SetFacingTo`) — field-verified in §12. Approach ordering preserves `MeleeCombatStrategy` with `CHASE_MOTION_TYPE` at `ATTACK_DISTANCE`.

*Acceptance:* A6, A12–A15. Extra probe: enemy with `SPELL_AURA_DAMAGE_SHIELD` or `SPELL_AURA_REFLECT_SPELLS` ≥10% max HP remains unattacked (`e9e7009` behavior).

### Feature G — Intent / state model

See §7. Matches legacy's `BotState` (COMBAT/NON_COMBAT/DEAD/REACTION) but collapses to `Intent` + `CombatState` because `DEAD` is a `Player` flag and `REACTION` (`FaceTargetUpdateDelay`, `ThinkUpdateDelay`) is only needed when reaction strategies (`AvoidAoe`, `SpecificCreatures`) are present — they are not in MVP.

### Feature H — Strategy / Trigger / Action

*What each solved in legacy:*

* **Strategy** solved *composability* (assemble a bot from role + race + dungeon + control strategies per class/spec/role).
* **Trigger** solved *when to consider an action* (poll predicate → `NextAction` basket).
* **Action** solved *how to do it* (execute + `isPossible` + `isUseful`).
* **Context/Value** solved *shared facts without recomputation* (cached values like `follow target`, `attackers`).

*MVP stance:* none of these need a framework now. Each has a 5-line pure-function replacement with the same semantics:

```cpp
bool InvalidTarget(Unit* t)   { return !IsValidHostileForAssist(bot, t); }
bool EnemyOutOfMelee(Unit* t) { return distance2d(bot,t) > meleeReach + 0.5f; }
Unit* AssistOwner(Player* m)  { /* selection path */ }
```text

and a `switch(intent)` + `if(combatState)` + `if(enemyOutOfMelee)` controller. The `StrategyContext` count (137+643) and normal-build action count (450+675) prove the framework cost outweighs the benefit at this slice's scope.

Recommendation below follows from that evidence.

---

## Addenda — architecture rules, Penqle module system, and hosting notes

### New core hook required?

**No new core hook for this vertical slice.** The three Phase 3 seams are sufficient:

* `SessionTransport::Headless` + `IsHeadless()` / `HasNetworkTransport()` / `InitHeadlessSession()` — already proven.
* `World::AddSession` queued path + `HasPendingSession`/`CancelPendingSession` + `World::InternalShutdown` drain — already proven.
* `IWorldUpdateListener::OnWorldUpdate(diff)` — proven tick.

World movement (`MotionMaster::MoveFollow/MoveChase/MovePoint`), lookup (`sObjectAccessor.GetUnit(bot,guid)`, `sMapMgr`), and chat (`ChatHandler` module command or `WorldSessionScript::OnPacket` filter) are all existing generic capabilities per `HOST_API.md:§1–§2`. Do **not** add `GetBot()`, `m_bot`, `IsBot()` checks or a per-`Player::Update` hook for this slice.

### Domain notes (module-system independence)

The behavior layer must not import the *temporary* bootstrap mechanism:

* `Custom WorldListener → WorldScript::OnUpdate` and `Custom Command → CommandScript` are future renames of `IWorldUpdateListener` and module `ChatHandler` registration. The controller should depend only on an abstract `void OnWorldTick(uint32 diff)` callback and an abstract `void SetIntent(Intent)` entry — the host adapter injects whichever hook the current core branch exposes.
* Pending Penqle module-system issue: `WorldScript::OnStartup` currently fires before world data (e.g. item templates) is loaded, unlike AzerothCore's `SetInitialWorldSettings` tail position (`HOST_API.md:§10` note). Behavior code must not load spell/talent/item data in `OnStartup`; do it on first tick or on explicit `OnWorldInitialized` rather than tying itself to a brittle startup signal.

### Transport/safety defaults (carry from Phase 3)

* Headless sessions use `NullSessionAnticheat` directly; never `InitAntiCheatSession(&K)`.
* Bot characters are normal existing alts on the human account. One account supports one Network session plus multiple character-keyed Headless sessions; human reclaim stays identity-based (`A17`) without changing duplicate-Network-login policy.
* Security predicate (`CanControl`) stays permissive for MVP (same-account or in-group of same account); guild/cross-account escalation gated behind `AllowGuildBots`/`AllowMultiAccountAltBots` and real rank checks, not hardcoded `accountId==4` elevation (audit in `HOST_API.md §10`).

---

## Final report (as requested)

### 1. Five most important things legacy PlayerBots taught us about basic bot behavior

1. **Follow is a dead-zone, not a chase.** `1.5 y` plus a restart guard on the same active native target/moving state is what stops jitter — not a smarter path. The failures were all jitter and oscillation, not navmesh misses.
2. **Assist is selection, not scanning.** For owned bots, the only target-creation rule worth keeping is "adopt master's current selection if valid hostile within 75 y; otherwise none." Every broad scan is deferred cost.
3. **Validation is the behavior.** The most fixed surface since May was the `IsValid` predicate (`friendly/dead/flag/range/LOS/immune/thorns`) — not combat rotations. Slice 3 before Slice 4 for a reason.
4. **Combat is two operations plus bookkeeping.** `bot->Attack(victim, melee)` / `AttackStop` plus the five-line bookkeeping (selection, facing, pet command, loot push on death, `invalid target` clear) is the real combat model. Everything else is multipliers.
5. **Return is explicit.** `Stop → previous intent` with a saved `WorldLocation anchor` is why stayed bots "come home" after combat instead of drifting to the fight location. Implicit return has never worked.

### 2. Five biggest pieces of legacy architecture/debt to avoid

1. String-keyed `StrategyContext` / `TriggerContext` / `ValueContext` registries + dynamic rebuilding each tick (4×5M rebuilds/hr, perf bug).
2. 450 shared + 675 class actions routed via an `ActionBasket` queue with `IterationsPerTick` + multipliers ("try unknown" logging) instead of a typed controller switch.
3. `TravelMgr`/`TravelNode`/`FutureDestinations` A* graph + `FlyDirect`/transport/teleport lattice used inside `follow` decisions (rpg-vs-travel oscillation).
4. 137-key chat prefix parser with `CommandSeparator "\\"` stacking, `queue <cmd>` timing, `#a` addon indirection, and 10–20 s BOT_TEXT dedup delay.
5. `sPlayerBotMgr` / `m_playerbotMgr` / `m_playerbotAI` field on `Player` + `sObjectMgr.GetPlayer(guid)` per group member per trigger instead of `BotManager` keyed by `ObjectGuid`.

### 3. Recommended minimum TortoiseBots behavior architecture

`BotManager (map<guid, BotEntry{record+controller}>)` → `BotController(Intent Follow|Stay, transient CombatState Approach|Attacking, prevIntent depth-1)` + three pure helpers `Movement(MoveFollow/MoveChase/Hold/ReturnToAnchor)`, `Targeting(IsValidHostileForAssist, AssistOwner)`, `Combat(Start/Stop/TickApproach)` driven once per `IWorldUpdateListener::OnWorldUpdate` tick and commanded solely via `controller.SetIntent(intent)` from `commands/BotCommands`. No `Player` field, no string registry, no graph, no world scan per tick. See §6 for ownership and §7 for the state diagram.

### 4. Strategy / Trigger / Action — MVP or wait?

**Wait — with an explicit reservation.** The MVP ships explicit predicates (`InvalidTarget`, `EnemyOutOfMelee`) and a typed `switch(intent)` controller that already captures the useful *semantics* of Strategy/Trigger/Action. Introduce a tiny composition registry (list of `Strategy{triggers,multipliers}` objects keyed by string) **only when** role-based combinations (tank vs heal vs DPS) or dungeon encounter behaviors actually need additive assembly — i.e. Phase 5's "2 humans + 3 bots" dungeon MVP. The trigger/action/value *tests* are useful; the trigger/action/value *registries and engines* are deferred.

### 5. Exact first implementation slice

Slice 0 (stubs + world-tick wiring) → **Slice 1 (Follow, 1.5 y)** → Slice 2 (Stay anchor + resume) → **Slice 3 (IsValidHostileForAssist + AssistOwner predicate)** → **Slice 4 (CombatState suspend/resume + Attack/AttackStop + chase approach)** → Slice 5 (remove/cleanup polish + 30-min journey). Slices 1 and 3–4 are must-demo; 2 and 5 are small companions. See §9 for file-level plan and 13-point journey.

### 6. What should be directly ported rather than reimplemented?

* **`AttackAction::Attack` ordering** (selection set, `oldTarget` save, `available loot` push, `SetFacingTo`, shield `AttackStop` guard, `pet COMMAND_ATTACK` / `REACT_PASSIVE`, `PlayAttackEmote`, `Attack(meleeFlag)`, `OnCombatStarted`) — port the *ordering* verbatim (`playerbot/strategy/actions/AttackAction.cpp:100–230`, fix `e9e7009`). A re-implementation that reorders pet/facing/attack leaks bugs that were already fixed.
* **`SelectNewTargetAction` stop ordering** (push corpse to loot, clear `attack target`, save `oldTarget`, null `current target`, clear selection, `InterruptSpell`, `AttackStop`, pet `COMMAND_FOLLOW`) — same shape (`strategy/actions/ChooseTargetActions.cpp:130–210`).
* **`PossibleAttackTargetsValue::IsValid` / `PossibleTargetsValue::IsValid` flag and range checks** (alive, same map, hostile, `CanAttack`, `NOT_ATTACKABLE_1/UNTARGETABLE`, within 75 y, `SpiritOfRedemption` check, `sightDistance`) — port the predicate set (`strategy/values/Possible*Value.cpp:313` ff., fixes `998c521e`/`c32d4fb4`/`347d9acb`).
* The **thorns/reflect ≥10% max-HP gate** (`e9e7009`) inside `AttackAction::Attack` that calls `AttackStop` — port, not "simplify away".
* Everything else (**intent model, Movement wrapper, AssistOwner priority, tick throttle**) is **reimplemented** as smaller typed code — copy the test, not the framework.

### 7. New core-hook requirement

**None.** The three Phase 3 seams (`SessionTransport`, queued `AddSession`, `IWorldUpdateListener`) plus vanilla `MotionMaster`/lookup/chat services are sufficient for the whole 13-point journey. The only near-hook would be a generic `ChatHandler::RegisterModuleCommands` if `CommandScript` registration is unavailable when the slice lands — that hook is **module-system** scope, not behavior scope, and is documented as optional in `HOST_API.md:§4`.

### 8. Ready to start Phase 4?

**Yes** — pending the five open runtime probes in §11 (none block the first slice). The host seam is frozen, `BUILD_PLAYERBOTS=OFF` is the default, no trigger/graph dependency exists, and the slice order in §9 gives a demoable bot at the end of Slice 1. The first command after this doc review should be: create `runtime/BotController.h/.cpp`, `behavior/Movement.h/.cpp`, `behavior/Targeting.h/.cpp`, `behavior/Combat.h/.cpp`, wire `BotManager::OnWorldUpdate`, and prove `follow` on `Dudette` — then proceed slice by slice with the `HOST_API.md:§10` SQL audits and §10 matrix gates.

---

*Deliverable path:* `docs/PHASE4_CORE_BEHAVIOR_DISCOVERY.md` (this file). No production `src/` was modified during this discovery.
