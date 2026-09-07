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

| Milestone | Branch | PR | Status |
| --- | --- | --- | --- |
| M0 baseline | `migration/m0-baseline` | https://github.com/Sagiroth/TortoiseBots/pull/70 (draft) | census committed; awaiting review; DO NOT MERGE without authorization |
| M1 diagnostics | `migration/m1-diagnostics` (stacked on M0 until merge) | https://github.com/Sagiroth/TortoiseBots/pull/71 (draft, stacked on #70) | code + checker self-test done; production-log + cost gates pending |
| M2 engine | `migration/m2-engine` (stacked) | https://github.com/Sagiroth/TortoiseBots/pull/72 (draft, stacked on #71) | mapping + verdict done; runtime proof pending |
| M3 spells | `migration/m3-spells` (stacked) | (pending) | coordinate fix done; 3 audits running |

M4–M10 branches not yet created. No merges authorized.

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
- [ ] Runtime proof (gear/travel/unlearn/cast observations); stacked draft M3 PR next

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
| Class packets | C-WAR C-PRI C-MAG C-ROG C-HUN C-PAL C-SHA C-DRU C-WLK | M5 (first slices) → M6 | not started |

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
- [x] Stacked draft M1 PR opened: https://github.com/Sagiroth/TortoiseBots/pull/71 (base: migration/m0-baseline)

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
