# Migration progress ledger (M0–M10, C-*, F01–F26)

Plan: `../../BEHAVIOR_MIGRATION_PLAN.md` (workspace root, single authoritative plan).
Architecture authority: `../PLAN.md`. Host contract: `../HOST_API.md`.

## Pins (M0 baseline, verified 2026-09-07)

| Role | Checkout | Branch | HEAD |
| --- | --- | --- | --- |
| Implementation | `TortoiseBots` | `migration/m0-baseline` (base `main`) | `e00857800c98abfba5a277dee080550a265e8e71` |
| Target core | `tortoise-wow` | `tortoise-bots` | `c12bb16ede94ae461495e9f62380c3d92c9f3278` |
| Gameplay donor | `playerbots-references/mod-playerbots` | detached | `5397110cba484a9b7209bc9f632652e9d4bd6a70` |
| Turtle runtime donor | `playerbots-references/shyalya-tortoise-wow` | detached | `49d183a086d0a51be14972deb5d707716dfabe6a` |

Working tree at M0 start: `TortoiseBots/docs/README.md` modified (plan index entry, preserved, uncommitted);
core `src/shared/revision.h` untracked (left untouched). Core `tortoise-bots` is 7 commits
ahead of `fork/tortoise-bots`, all upstream merges (`c12bb16`, `74410e8`, `55fdbc0`, …), no local edits.

Donor checkouts are read-only. No builds, no docker, no gameplay run yet in this migration.

## Milestone branches/PRs

CONSOLIDATED (per owner instruction 2026-09-07): the full stack lives on ONE
branch and ONE draft PR. Stacked drafts #70–#80 are closed as superseded
(identical commits). Per-milestone history is preserved as 16 individual
commits on the branch. Prior per-milestone rows are kept below for archaeology.

| Milestone | Branch | PR | Status |
| --- | --- | --- | --- |
| M0–M10 (all) | `migration/behavior-migration` (19 commits from `main`) | https://github.com/PiotrZadka/TortoiseBots/pull/83 (draft → `main`) | open; ON+DISABLED matrix BUILT ✓; runtime gates pending |

| M0 baseline | `migration/m0-baseline` | #70 (closed, superseded by #83) | merged into singular branch |
| M1 diagnostics | `migration/m1-diagnostics` | #71 (closed, superseded by #83) | merged into singular branch |
| M2 engine | `migration/m2-engine` | #72 (closed, superseded by #83) | merged into singular branch |
| M3 spells | `migration/m3-spells` | #73 (closed, superseded by #83) | merged into singular branch |
| M4 party | `migration/m4-party` | #74 (closed, superseded by #83) | merged into singular branch |
| M5 slices | `migration/m5-slices` | #75 (closed, superseded by #83) | merged into singular branch |
| M6 classes | `migration/m6-classes` | #76 (closed, superseded by #83) | merged into singular branch |
| M7 world | `migration/m7-world` | #77 (closed, superseded by #83) | merged into singular branch |
| M8 content | `migration/m8-content` | #78 (closed, superseded by #83) | merged into singular branch |
| M9 services | `migration/m9-services` | #79 (closed, superseded by #83) | merged into singular branch |
| M10 closure | `migration/m10-closure` | #80 (closed, superseded by #83) | merged into singular branch |

Build basis: pristine worktree at branch tip + tortoise-wow c12bb16e.
No merges authorized.

## M2 gates (plan §4)

- [x] M2 enabler: `S:init done` anchor at every Engine::Init (commit on this branch)
- [x] Dual-hook verdict (see M2-DUALHOOK-NODUP): complementary by design, zero true duplicates in 53 generic + 9 class dirs; neither entry point retired
- [x] Tie semantics documented (first-max-wins; equal-relevance keeps older basket)
- [x] Minimal-break divergence verified as deliberate bounded fix (donor continue busy-loops)
- [x] Lifecycle parity: trigger latch/cadence, expiry rule, PushAgain offsets, reaction gating, UpdateAI — all donor-identical; no defect, no behavior change
- [x] Deferred-reinit preservation intact (ChangeStrategy signature guard + Reset deferral; BGTactics WSG comment case documented in code)
- [ ] Runtime proof of required cases (server run); stacked draft M2 PR next

## M3 gates (plan §4)

- [x] Coordinate-cast outcome fix (rejected ground cast returns false; callers fall through)
- [x] SpellId rank audit: donor-identical rank logic; permissive-isUseful KEPT (fallback chains depend on it); 5s TTL staleness bound, no code change
- [x] Talent audit: largest-tree inference + owned-build preservation verified; gaps recorded (no Goblin/High Elf racials; secondary bear/cat sites) — F07/C-DRU follow-ups
- [x] Shim audit: D1/D3/D4/D5 fixed with quoted native contracts (all consumers verified); D2 deferred with data requirement; rest classified harmless/by-design
- [ ] Runtime proof (gear/travel/unlearn/cast observations); M3 PR: https://github.com/PiotrZadka/TortoiseBots/pull/73

## M4 gates (plan §4)

- [x] Targets: explicit > RTI > scan; lazy invalidation mapped; reach rebuilt per-tick (no stale); CC strip by construction (anchor-hole report corrected)
- [x] Interrupt/CC command paths probe mature graph, revalidate, no side engine; RemoveExpired contradiction resolved (both engines expire)
- [x] Guard/Free fix: central setter closes stale-reaction-follow hole; formation handoff verified clean; pull ownership distinct; summon wart noted-not-changed
- [ ] Runtime party proof (A02/A03/A06/A08/A09/A10); M4 PR: https://github.com/PiotrZadka/TortoiseBots/pull/74

## M5 gates (plan §4)

- [x] 5 slice audits landed (WarProt/PriestHoly/Mage/Rogue/Hunter): inventories + donor gaps + Turtle data checks, all audit-only
- [x] Pet-CC fix: AttackAction pet AttackStart now respects the CC strip (mirrors selection; skull-ignore-RTI honored)
- [x] Inert-file traps recorded (hunter Generic disengage, rogue Dps, warrior Tank, mage dead scorch reg): never registered; disposition = never-activate guard, not deletion
- [x] C-packet rows for 5 slices; remaining specs/classes queued to M6
- [ ] Party runtime proof (human+4bots dungeon); M5 PR: https://github.com/PiotrZadka/TortoiseBots/pull/75

## M6 gates (plan §4)

- [x] 4 class audits landed (Pal/Sham/Dru/Wlk): inventories + donor gaps + data checks
- [x] Paladin Pve-combat blessing fix (PvP tables → pve; all four actions registered)
- [x] Pet guard extended with damage-immunity gate (banish/invulnerable; warlock audit caught it)
- [x] Nine-class matrix: all 9 covered (5 in M5 + 4 in M6); DK excluded by design; no tree silently falls back (placeholders verified zero-hook + update-wired)
- [ ] Spec/role runtime rotation proof (all classes); M6 PR: https://github.com/PiotrZadka/TortoiseBots/pull/76

## M7 gates (plan §4)

- [x] F01/F02/F03 quest audit: paths complete; blacklist parity restored (50000); escort absent both sides (parity); sync-ForPlayer undocumented (conf gap)
- [x] F04 audit: Shyalya corrections queued (crowd/RNG/patrol/taxi-cheat); avoid-list absent + avoid-area fail-closed + spell-click absent (no core support)
- [x] F05 fixes: rejected-leg retained; RPG-taxi 100k leak plugged (4 restores); generators force-off verified; empty-table degradation mapped
- [x] F06 audit: path map + H1-H7 hazards recorded; D1 = host gap (no native CanLoot; late skip holds) deferred to compilable env
- [x] F07 audit: idempotent steady-state verified; modern managers correctly absent; no change
- [x] F08 audit: 3-layer coverage mapped; feed stub = parity (all trees cheat); recovery candidates scoped; no stable import
- [x] F01/F11/F12 audit: mode table + precedence; gaps G1-G5, L1-L12 recorded (M9 implementation scope)
- [x] F22/F23 audit: schema-only ships, clean-install safe, owned data preserved; buff/chat at parity (both inert); GetValues also starves LoginCriteria
- [ ] Journey runtime proof (P01-P08, P22-P23); M7 PR: https://github.com/PiotrZadka/TortoiseBots/pull/77

## M8 gates (plan §4)

- [x] Raid audit: thin generic + 4 object/hazard behaviors; Onyxia empty; 4H teardown + Naxx enter/leave FIXED (string-only, both sides verified); void-zone creators still missing (needs new classes — deferred)
- [x] Turtle map: 30 mechanic rows, zero bespoke tactics, generic-only participation; LFT aliases for 4 maps; KARAZHAN guard blocks all 15 Kara files (narrowing queued)
- [x] BG audit: WSG/AB/AV objective play mapped; SV + BR zero tactics; Eye/Isle correctly absent; no DungeonClear dependency anywhere
- [ ] Encounter/match runtime proof; M8 PR: https://github.com/PiotrZadka/TortoiseBots/pull/78

## M9 gates (plan §4)

- [x] F09/F10: personal paths intact; lowest-buyout per-unit FIX; full-market 12-gap table; companion core seam SPEC (snapshot/Guard/Post/Bid/Expire)
- [x] F13: group/guild flows mapped; hardcore/level-gate gaps; task-loop surface queued to M10 design
- [x] F14: demand-only verified; LFT conf comment FIXED (hardcoded table, not DBC); kit-based roles + autonomous policy queued
- [x] F25: no-arbitrator verdict (emergent guards); retry absent; restart orphans; measurement plan written
- [ ] Service runtime proof (P09-P18, P25-P26); M9 PR: https://github.com/PiotrZadka/TortoiseBots/pull/79

## M10 gates (plan §4)

- [x] F16/F17/F18: uncovered-command list; preset gaps; F18 BLOCKED verdict (no seam, no false implementation)
- [x] F19/F20: native-routed transactions (mail send gap); GetValues FIXED module-only via native GetRootSections/GetKeys; tweakValue/guildMaxBotLimit noted
- [x] F21: 5 server-without-UI gaps; discovery stub; ledger tally 102 rows, zero overclaims; 3 hygiene flags fixed
- [x] F26: KARAZHAN narrowing spec (no edit); RUNBOOK.md + KNOWN_LIMITATIONS.md; release table below; stacked M10 PR next

## Release capability table (F26 — audit state, NOT a parity claim)

- Source-complete: M0–M3 contracts, M4 tactics map, 9/9 class audits, F01–F08/F11–F13/F22–F23 audits, F24 maps, F09/F14/F15/F25 policy audits, F16–F21 control audits.
- Code-fixed (uncompiled): M1 trail, M2 anchor, M3 coord+shim×4, M4 guard/free, M5 pet-CC+immunity, M6 pve-blessings, M7 blacklist+taxi×2, M8 naxx×2, M9 lowest-price, M10 GetValues.
- Blocked: F18 self-bot, F10 synthetic market (core seam), F06-D1 (compile), D2 (DBC data), all runtime proof.
- Excluded: DungeonClear, DK/glyph/vehicle/arena, Eye/Isle, LLM generation, WotLK/TBC-only, guild vaults.
- Deploying any of this requires: build matrix, guard scripts, disposable-fixture journeys, user playtest gates, explicit merge authorization per PR.

## Packet ownership (plan §7, §14)
| Stage | Packets | Owner milestone | State |
| --- | --- | --- | --- |
| Inventory and foundation | M0–M3; F01, F20, F22 discovery | M0 (census), then M1/M2/M3 | M0 committed (PR #70 draft); M1 next |
| First owned-party acceptance | M4–M5; F02, F05–F08; F16–F17 | M4/M5 | not started |
| Autonomous world | F03–F04; F11–F12; F13 | M7 | not started |
| Economy | F06/F19 → F09 → F10; with F11–F12/F25 | M7/M9 | not started |
| Queues/content | F14/F15; F24 + M6 matrix | M6/M8 | not started |
| Controls and operations | F16–F18; F21; F20/F22/F23 throughout | M7/M10 | not started |
| Completion | F25 soak, F26/M10 | M9/M10 | not started |
| Class packets | C-WAR C-PRI C-MAG C-ROG C-HUN (M5 audited) + C-PAL C-SHA C-DRU C-WLK (M6 auditing) | M5 (first slices) → M6 | audits done/in-flight; implementation queued behind compile+runtime |

## M0 gates (plan §4)

M0 committed on `migration/m0-baseline`, draft PR #70 open. Census complete (4/4 scouts),
guards re-run OK. Runtime gates all pending by rule.
- [x] Capability ledger (CAPABILITIES.tsv) + graph inventory (~111 strategies, ~250 actions, ~225 triggers, ~270 values, 9 class contexts, 4 services)
- [x] `ConfigAccess::GetValues` empty, `LoadAuctionPrices` clear-only, `GuildBankAction` ZERO-false, Engine dual hooks, Queue name-identity, SpellId numeric branch
- [x] Turtle Karazhan native scope; CMake `KARAZHAN` denylist narrowing flagged for M0/M8 (unchanged)
- [x] Modern corrections (no src/ahbot/AhAction/price-helper/LLM; 9+Dk dirs); dungeon-clear exclusion at `modules/mod-dungeon-clear/**`
- [x] Guards re-run OK; draft M0 PR #70 open
- [ ] Activation proof needs M1/M2 runtime graph dump

## M1 gates (plan §4)

- [x] Trail carries bot/state/decision identity: TICK `state=` + `strats=` (Engine.cpp BotStateName + StrategySignature)
- [x] Trigger/action/target + base/effective relevance on T/PUSH/A lines; per-multiplier factors (MULT lines, not just zeroing)
- [x] Usefulness/possibility rejections distinguishable: USELESS vs IMPOSSIBLE vs FAILED vs UNKNOWN with src/base/eff
- [x] Prerequisite/alternative/continuation visible via PUSH `(prereq|alt|cont|again)` + PREREQ lines
- [x] Spell attempt + native preparation result: CAST_START + CAST_FAIL/OK `phase=PREPARE*` on unit/GO/coordinate overloads; 7 CAST_GATE early-exit reasons
- [x] Checker `tools/check_decision_trail.py --self-test` passes (grammar + all 4 M1 signatures + malformed detection)
- [x] M1 finding: no module-visible EFFECT-phase completion signal exists (free hooks have no callers); generic core proposal deferred with evidence, not silently dropped
- [x] Disabled-cost analysis: Open() gates on EnableActionLog (BotActionLog.cpp:95); disabled Write = mutex + map miss + flag branch; measurement pending server run
- [ ] Production-log validation: checker run against real `logs/bots/*.log` from disposable fixture (needs server run — NOT docker-blocked for user, pending)
- [ ] Bad-spell-choice end-to-end explanation intent→result (needs runtime trail)
- [x] Stacked draft M1 PR opened: https://github.com/PiotrZadka/TortoiseBots/pull/71 (base: migration/m0-baseline)

## Open blockers (all honest, none silent)

1. No builds available in this environment (user: no docker checks) — M1 C++ diffs are logging-only on existing APIs, reviewed line-by-line; compile + production-log gates pending server run.
2. Runtime gates: no client observation yet — all A01–A16/P01–P26 remain pending by rule.
3. No gameplay certification claimed anywhere.

## Decisions

- M0 branch `migration/m0-baseline` from `main`; M1 stacks `migration/m1-diagnostics` on M0 until merge. User `docs/README.md` index edit preserved uncommitted.
- Integrator rule (plan §7): shared Engine/Strategy/AiFactory/host changes go through one owner (this session).
- M1 C++ is logging-only (no behavior change) except zero new branches on cast success paths; the one dropped-return mistake was caught in diff review and restored before commit.
- No donor code imported in M1; telemetry is module-owned.

## Exact next action

1. Commit M1 (Engine + PlayerbotAI trail, checker, ledgers); push; open stacked draft PR (base: migration/m0-baseline).
2. Start M2 on `migration/m2-engine` (stacked): trigger/queue/strategy lifecycle specification + tests.
3. Real-client scenarios stay pending until user playtest; exact steps at M4/M5.
