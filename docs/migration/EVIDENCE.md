# Migration evidence log (plan §6 A01–A16, §15 P01–P26)

Rule: compilation, source presence and command ACKs never close a runtime gate.
Every scenario below needs fixture + revision/data/config + steps + observed results.
Status values: `pending` | `pass` | `fail` | `blocked(reason)`.

## M0 source-verification evidence (2026-09-07, no client, no server run)

| Check | Anchor | Result |
| --- | --- | --- |
| Pins recorded | T `e0085780`, CORE `c12bb16e`, M `5397110c`, S `49d183a0` | done |
| Engine dual hooks | T `strategy/Engine.cpp:112-134` | confirmed mixed interface |
| Queue name identity | T `strategy/Queue.cpp:10-33` | confirmed |
| SpellId numeric branch | T `strategy/values/SpellIdValue.cpp:169-181` | confirmed |
| GetValues empty | T `PlayerbotAIConfig.cpp:23-26` | confirmed gap |
| Auction mirror clear-only | T `runtime/PlayerbotRuntimeFacade.cpp:271-278` | confirmed gap |
| GuildBank false ZERO | T `strategy/actions/GuildBankAction.cpp:11-39` | confirmed |
| Turtle Karazhan native | CORE `ScriptLoader.cpp:291,305`, `src/scripts/dungeons/{karazhan_crypt,lower_karazhan_halls,upper_karazhan_halls}` | confirmed native scope |
| Guard scripts | `tools/verify_turtle_surface.sh`, `tools/verify_penqle_host_contract.sh --core ../tortoise-wow` | re-run pending in M0 PR validation |

## Shared acceptance scenarios A01–A16 (all pending)

| ID | Setup and trigger | Required outcome | Status | Evidence |
| --- | --- | --- | --- | --- |
| A01 | Owned bot login, repeated add/remove, owner reconnect/reclaim | One AI owner; durable owner; saved build unchanged | pending | — |
| A02 | Follow → Stay → Come → Follow in/out of combat | All engines agree; no reaction fighting next command | pending | — |
| A03 | Explicit ranged attack before threat, then target death/evade | Requested target survives pre-threat; stale clears, assist resumes | pending | — |
| A04 | Reach-then-cast; target dies/moves/maps or command changes | Valid prerequisite survives; invalid cancels bounded | pending | — |
| A05 | Legal + deliberately rejected unit/ground/item/pet casts | Acceptance matches native preparation; later failure distinguished | pending | — |
| A06 | Multi-mob tank + healer/DPS | Pickup, threat rules, no default-action starvation | pending | — |
| A07 | Tank emergency, injured DPS, incoming heal, remote member | Correct urgency/target/rank; no unsigned-health error | pending | — |
| A08 | Interruptible cast, immune target, cooldown/range variants | Eligible executor interrupts in window; expired request never fires late | pending | — |
| A09 | Assigned mark + CC with AoE on/off, pets, cleave | CC lands/reapplies; group damage respects protected target | pending | — |
| A10 | Pull and pullback; blocked path/timeout/owner disappears | One target; pullback returns to anchor; invalid cancels | pending | — |
| A11 | Kill/loot quest, tapped/empty/skinnable/full-bag corpses | Native rights/rolls respected; no crouch/reopen loop | pending | — |
| A12 | Single death, out-of-range corpse, total wipe | Correct rez/corpse-run; regroup; dead engine exits cleanly | pending | — |
| A13 | Dungeon portal, far/near teleport, exit, transport, rejected summon | Native transition; AI pauses/resumes; rejection not arrival | pending | — |
| A14 | Consecutive pulls deplete mana/ammo/reagents/durability | Recovery/restock or reported inability; no unusable rotation | pending | — |
| A15 | Talent/role change, saved preset, relog, no-op talent query | Only real topology rebuilds; owned build preserved | pending | — |
| A16 | Optional service selects bot then human assumes control | Service releases ownership; no unwanted side effects | pending | — |

## Feature scenarios P01–P26 (all pending; fixtures per F-packet during M5–M9)

P01 owned→autonomous→recall; P02 quest sync directions; P03 autonomous quest journey;
P04 blocked objective/full log/restart; P05 taxi/boat/vendor route; P06 crowded RPG hub + avoidance;
P07 gather→craft→mail/sell; P08 random creation→progression→restart; P09 AH price fixtures;
P10 sell/bid/outbid/buyout/cancel; P11 empty-market supplier+buyer; P12 stale auction/reclaim;
P13 market reload/override/rebuild; P14 population timers/pins/restart; P15 recruit/release/world group;
P16 guild flow; P17 LFT missing role/timeout; P18 BG demand + match; P19 bulk mixed-authority;
P20 preset migration; P21 self-bot/free-alt/always-online; P22 world-buff config reload;
P23 clean install + data import; P24 dungeon/raid without DungeonClear; P25 simultaneous services + takeover;
P26 multi-party soak with budgets. All `pending`.

## M1 decision-trail evidence (2026-09-07, branch migration/m1-diagnostics)

### Emitter grammar (implemented, uncompiled — no docker builds per user constraint)

- `Engine::DoNextAction`: TICK carries `state=` + `strats=`; T/PUSH/A lines carry
  `src=` (+ `base=`/`eff=` on A lines); non-1.0 multiplier factors logged as MULT lines.
- `PlayerbotAI::CastSpell` unit/GO/coordinate overloads: CAST_START + CAST_OK/FAIL
  `phase=PREPARE*`; 7 CAST_GATE early-exit reasons (self-harmful, pet-redirect,
  flying, stand-or-facing-delay, moving-jump-fall, moving-no-master, loot-impossible).
- Coordinate overload result still ignored by design (M3 owns the fix); now logged
  as `phase=PREPARE-coord-ignored` so rejected ground casts are visible.
- All behavior paths preserved (diff review: every early-return intact; the one
  dropped `return false` introduced mid-edit was restored before commit).

### Checker

- `tools/check_decision_trail.py --self-test`: PASS (grammar + UNKNOWN, stale-retry?,
  multiplier-zero, cast-fail/gate signatures + malformed-line detection).
- Production-log run pending server execution; `--strict` gates M1 runtime closure.

### Findings

- No module-visible EFFECT-phase completion signal exists (free-function hooks in
  BotActionLog.cpp have no callers; core owns spell effects). Generic core
  completion/failure proposal deferred to M3/M10 with this evidence — not dropped.
- Disabled cost: `Open()` returns null when `EnableActionLog=0` (BotActionLog.cpp:95);
  disabled Write = mutex + map miss + flag branch (+ one spell-map lookup for LogCast*).
  Measurement pending server run.
- A log label never converts an attempt into a successful heal/interrupt; A05/A07/A08
  remain pending real-client observation.

## M2 engine-mapping evidence (2026-09-07, branch migration/m2-engine)

Three read-only mapping scouts (generic 53 files, 9 class dirs, queue/trigger/reaction spec)
plus parent verification of Strategy.h/Strategy.cpp/CombatStrategy.h.

- Dual-hook verdict: no strategy implements both forms with content. Generic files are
  legacy-list-only; modern vector hooks are `{}` everywhere (explicit blockers only in
  CombatStrategy.h/NonCombatStrategy.h). Class dirs are single-form-or-placeholder
  (spec bare names contribute zero hooks; rotation attaches via strategiesToUpdate).
  Intra-legacy same-names across states are by-design per-state registration; the
  dispatcher calls exactly one state branch per Init. No hook retired.
- Tie: `Queue::Pop` strict-`>` first-max-wins (Queue.cpp:35-61); queue identity is
  name-only, so an equal-relevance changed-target push keeps the older basket/event.
- Minimal `break` vs donor `continue`: Tortoise defers the queue intact (bounded);
  donor re-peeks to budget exhaustion. Deliberate fix, preserved.
- Parity (donor-identical, no change): trigger latch/cadence/Check/Reset pump,
  RemoveExpired rule (ExpireActionTime=5000 default), PushAgain +0.02/+0.01/+0.03
  offsets, ReactionEngine body (>100U active, no preemption, interrupt-on-discovery
  only), UpdateAI reaction gating, SetActionDuration routing.
- Donor-only machinery NOT ported (no proven need): failure-retry/generation guards,
  testMode gate, PerfMon spans, forceActivity/teleport/AFK wrapper extras are
  Tortoise-side and preserved.
- Runtime proof of the required M2 case list (two-triggers-one-action, expired target,
  reset-during-execution, Stay→Follow, repeated pulls, no-action tick, minimal
  emergency) needs a server run; the M1 checker grammar covers trail-shape only.

## M3 spell/talent/shim evidence (2026-09-07, branch migration/m3-spells)

Three read-only audits plus parent verification of every fixed native contract.

- Coordinate fix: `CastSpell(x,y,z)` ignored `prepare()` and returned true. Now
  returns false on rejection; CastCustomSpellAction callers fall through to
  self-cast fallback instead of announcing phantom ground casts.
- Rank parity: Tortoise/ donor rank blocks functionally identical (pet-field and
  IsHighRank alias noise only). Mana-save numeric walk is real-but-bounded risk,
  unreachable at default saveMana 1.0; chain data inspectable in
  tw_world_spell_chain.sql, Rank labels need client DBC extraction.
- isUseful stage: donor HasSpell/vehicle gates fail faster; Tortoise defers to
  isPossible with the SAME winner (holy-shock→holy-light, regrowth→touch,
  repentance→shield→light fallbacks quoted). Kept permissive; revisit only on
  trail-measured reach waste.
- Staleness: spell-id value 5s TTL + context Reset cover values; action-member
  spellId has no TTL. Worst case bounded + self-healing; no change without compile.
- Talents: largest-tree + pre-10 defaults; bear/cat forced-role-first at primary
  site; owned builds preserved (IsOwnedBot guards); rebuild via snapshots+reset.
  Gaps: RacialsStrategy classic-only (Goblin/High Elf racials absent); secondary
  feral sites use Primal-Fury/coin-flip.
- Shim D1/D3/D4/D5 fixed, each with quoted native header + exhaustive caller list.
  D2 (TARGET_FLAG_LOCKED) deferred for DBC-mask evidence. D6 left (defensible).
  IsSpellReady narrowness, isMoving approximation, distance hysteresis, collapsed
  flags, SEC mapping, quest writers — all classified by-design with rationale.

## M4 party-tactics evidence (2026-09-07, branch migration/m4-party)

Three read-only audits plus parent verification of every touched path.
Two scout claims corrected by direct source check.

- Targets: explicit-command > RTI > role scan (Dps/TankTargetValue); lazy
  invalidation (world/map/selection/desync/death/own-death); reach prerequisite
  rebuilt per tick from spell+target (no stored Unit, no cross-cast staleness).
- CC: PossibleAttackTargets strips breakable/unbreakable CC before selection;
  FindTarget consumes the stripped list. The reported AoE-anchor hole does not
  exist (candidate set pre-stripped); residual splash is spell-choice scope.
- Pull: Tortoise completion marker + anchor pullback improve on donor
  timeout-only; owner-disappears bounded by 15s cap in both trees.
- Interrupt/CC commands: capability probe → revalidate → direct exec → queued
  PASSTHROUGH=100 fallback with mature reach → forced tick. No side engine,
  no class table. CORRECTION: main-engine queue DOES expire (Engine.cpp:407).
- Guard/Free fix: shortcuts bypassed the central setter (NC+C only), leaving
  reaction-follow stale. Now routed via SetMovementStrategy; Reset and guard
  position bookkeeping preserved. (One dropped Reset() caught + restored pre-commit.)
- Formation: value swap + single FOLLOW clear + MoveFollow dedup = no churn.
- Summon binding-order wart noted, unchanged (needs runtime harm proof).
- Heal urgency/dispel/rez mapped (PartyMemberToHeal tiers, incoming adjustment,
  anti-double-heal reservation, dispel order, rez conflict guard); no rank
  optimizer exists (highest-known unless mana-save) — C-PRI follow-up in M5/M6.

## M5 slice-audit evidence (2026-09-07, branch migration/m5-slices)

Five read-only slice audits (one re-dispatched after a placeholder response).
No ports: implementation needs compile+runtime, both unavailable here.

- WarProt: full rotation inventoried; tank alias→placeholder→protection-pve
  activation; legacy Tank file inert. Gaps: CC-only interrupts, missing
  vigilance/ranged-pull/intervene, sunder>5 risk, TBC-legality checks each.
- PriestHoly: bucket ladder + PoH group AoE + dispel/rez/mana/shadow ladders;
  rank=highest-known CONFIRMED, no optimizer. Donor gaps all TBC (correctly
  absent). SW:Death kept — verify Turtle backport; prayer-of-spirit likewise.
- Mage: proc-blind core (WotLK N/A); fire-locked fallback; loop-guards verified
  on all vectors. TBC re-adopt list (ice lance/arcane blast/spellsteal/
  invisibility/water elemental) needs per-item Turtle legality.
- Rogue: thresholds + opener guards + fallbacks verified; no pooling/reservation
  (Turtle-applicable donor-mature — queued); expose-armor wire-or-remove.
- Hunter: active ZERO-vanilla stack verified (dead-zone, traps, aspects, ammo);
  specs pass-through; feed-pet STUB; distracting/wyvern dead regs. PET-CC FIX
  applied (pet respects CC strip). Inert Generic files (disengage+flee) must
  never be registered — recorded as guard, files untouched.
- Party runtime (human+4bots, rotating slots, dungeon with pulls/CC/interrupts/
  loot/recovery) remains the M5 gate; exact user steps deferred to pre-runtime.

## M6 class-audit evidence (2026-09-07, branch migration/m6-classes)

Four read-only audits; parent verified every fix. No ports.

- Paladin: dual-arch (old vector + new list) with priority arbitration, blessing
  role tables, fallback chains. FIX: Pve-combat blessing used PvP tables
  (copy-paste; Raid-combat already uses pve). Gaps: ArtOfWar dead trigger,
  untalented single-action fizzles, CC eligibility delegated to generic value.
- Shaman: OLD+NEW parallel implementations; shock without interrupt branch;
  totem rank tables + exclusions; chain-heal AoE-only; earth-shield raid-only;
  reincarnation unhandled. Leaks: range-mismatch churn, HaveAnyTotem no-owner
  suppression, triggers-while-moving, air-totem double registration.
- Druid: forced-alias arbitration reconciled across layers (role-first in
  factory, Primal-Fury fallback, THICK_HIDE/cat-form triggers). Gaps:
  lifebloom/starfall re-adopt, dangling triggers, prowl gates, bear charge.
- Warlock: Life-Tap double-guarded; no DoT lifetime guard; shard create/destroy
  triggers are dead wiring (no creators); pet spell-lock stack. Caught the
  banish hole in the M5 pet guard → extended with IsImmuneToDamage.
- Matrix: 9/9 classes audited. No applicable tree falls back to an unrelated
  strategy (placeholders contribute zero hooks; rotation attaches via update
  wiring). Raid-specific role checks deferred to M8.

## M7 world-utility evidence (2026-09-07, branch migration/m7-world)

Eight read-only audits (one blacklist fix, two taxi fixes). No ports.

- Quests: owner-assisted + autonomous journeys complete in source; cheats
  cheat-gated both sides; blacklist parity restored (Malkovich 50000);
  SyncQuestForPlayer missing from conf template (gap); escort/scripted driver
  absent both sides (parity, record honestly); full-log handling lossy.
- RPG: Shyalya crowd/RNG/patrol/taxi-cheat corrections identified, queued for
  compilable env (behavioral, need build). Avoid-list absent, avoid-area
  fail-closed, spell-click absent with zero Turtle core support found.
- Travel: purposes/costs/failed-edges/faction gates mapped; MinimalMove now
  retains rejected legs; RpgTaxi restores funding on all 4 reject paths;
  generators force-off (no PathInfo area query); empty tables degrade safely.
- Loot: full path map; H1-H7 hazards recorded as known limits; D1 admission
  permission is a host gap (Penqle has AllowedForPlayer per-item but no
  CanLoot; late skip prevents theft) — deferred, not bypassed.
- Gear: steady-state idempotent, first-init single-shot, full-randomize
  uncalled; modern item managers correctly not ported.
- Professions/pets: 3-layer coverage; feed stub is donor parity (all three
  cheat identically) — real-food is enhancement scope, not migration debt;
  stabling absent-by-design; train-pet gap; relog/travel via native+triggers.
- Population: mode table + precedence recorded; first-init pipeline (G1/G2),
  distribution weights (G5), event machine + clock-shift (L2/L6/L7), dynamic
  target (L1), revive/teleport scheduling (L4/L5) all MISSING — M9 scope.
- Data: clean-install safe everywhere; owned data never dropped; datasets need
  operator import workflow (F22). Buff/chat at exact parity (both inert via
  GetValues/Generate stubs); LoginCriteria also starved — F20 fix covers both.

## M8 content evidence (2026-09-07, branch migration/m8-content)

Three read-only audits; parent verified every fix. No DungeonClear contact.

- Raids: MC rune-movement/GO IDs match Turtle core (douse needs no item
  core-side — bot item requirement may fail closed; noted); BWL suppression
  GO/aura match but bot path is rogue-only while core allows any class;
  Onyxia fight strategy EMPTY (generic participation only); 4H teardown name
  fixed + Naxx enter/leave wired (all names verified registered); void-zone
  trigger/action have no creators (new classes needed — deferred to compilable
  env); most bosses in every raid have zero tactics. No encounter claims.
- Turtle: 6 dungeon dirs + SV inventoried (30 mechanic rows with NPC/spell
  IDs); zero bespoke bot tactics; LFT demand-fill aliases cover 4 maps
  (no Lower/Upper Kara, no SV). KARAZHAN CMake guard would reject valid Turtle
  Kara files verbatim — narrowing queued for M10 (needs file-by-file review).
- BGs: WSG flag play, AB node assault/defense with stickiness, AV objective
  tables mapped; death/spirit rules; interference guards hold. SV (id5) and
  BR arena (id4) have zero tactics; Eye/Isle correctly absent. Autonomous
  BG queue absent by design (demand-only).

## M9 services evidence (2026-09-07, branch migration/m9-services)

Three read-only audits; parent verified every fix. No ports.

- AH: personal sell/bid/buy intact via session handlers but price-blind;
  lowest-buyout now true per-unit min. Full market = 12-capability gap vs
  SAH; companion core seam specified (paged snapshot + Guard for F09 races;
  MarketPost/Bid/Expire reusing native persistence/mail for F10 synthetic
  supply). No direct map/DB writes in module; no fake parity.
- Groups/guilds: formation→accept→leave→release + charter→orders flows
  mapped; durable vs transient ownership distinguished; hardcore/level-gate
  gaps; modern task-loop surface queued.
- LFT: demand-only verified; role = talent-enum (kit check queued);
  autonomous grouping absent by design; conf comment corrected to hardcoded
  addon table.
- Scheduling: NO central arbitrator — emergent fail-closed guards; F25 retry/
  backoff absent; solo-idle triple-eligibility + restart orphans recorded;
  per-tick cleanliness verified by inspection; measurement plan written with
  stages/metrics/assertions, numbers pending runtime.
