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
| M0 baseline | `migration/m0-baseline` | https://github.com/PiotrZadka/TortoiseBots/pull/70 (draft) | census committed; awaiting review; DO NOT MERGE without authorization |
| M1 diagnostics | `migration/m1-diagnostics` (stacked on M0 until merge) | (pending) | next |

M2–M10 branches not yet created. No merges authorized.

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

- [x] Revisions + working-tree state recorded (above)
- [x] Capability ledger rows for every feature family with disposition (42-line CAPABILITIES.tsv: 10 verified-T rows, 8 modern-correction M- rows, 13 Shyalya S- rows, 10 Tortoise-graph T- rows)
- [x] Graph inventory: ~111 generic strategies, ~250 actions, ~225 triggers, ~270 values, ~17 multipliers, 9 class contexts, ~20 .bot verbs, 4 services, holder 12 + bot 42-key donor tables
- [x] Nine class factory entries traced (`AiFactory.cpp:22-82`, all 9 Vanilla contexts + generic fallback; DK dir empty, no case → base fallback)
- [x] Engine dual-representation confirmed (`Engine.cpp:112-134`, state-aware + vector hooks, dual defaults)
- [x] Queue name-identity confirmed (`Queue.cpp:10-33`)
- [x] SpellId mana-save numeric-order branch confirmed (`SpellIdValue.cpp:169-181`)
- [x] `ConfigAccess::GetValues` empty-body confirmed (`PlayerbotAIConfig.cpp:23-26`, WorldBuff load inert)
- [x] `LoadAuctionPrices` clear-only confirmed (`PlayerbotRuntimeFacade.cpp:271-278`)
- [x] `GuildBankAction` false-return ZERO path confirmed (`GuildBankAction.cpp:11-39`)
- [x] Turtle Karazhan scope confirmed native (Lower/Upper halls + Crypt scripts in core; CMake `KARAZHAN` denylist needs narrowing review — M0/M8 finding, not changed yet)
- [x] Modern donor corrections: NO src/ahbot, NO AhAction, NO median/lowest helper, NO LLM generator, 10 class dirs (9+Dk)
- [x] Dungeon-clear exclusion confirmed at `modules/mod-dungeon-clear/**` with full `.dc` surface (never in ledger)
- [x] Guard scripts re-run 2026-09-07: `verify_turtle_surface.sh` OK, `verify_penqle_host_contract.sh --core ../tortoise-wow` OK (core c12bb16e)
- [ ] Test fixtures + source/data pair reproducible (config full-key inventory landed from census; fixture skeletons pending M1)
- [ ] Unresolved name/activation ambiguities filed as issues (census done; activation proof needs M1/M2 runtime graph dump)
- [ ] Draft M0 PR opened with scope/validation/pending gates

## Open blockers (all honest, none silent)

1. Census complete (4/4 scouts). Activation proof needs M1/M2 runtime graph dump.
2. Runtime gates: no client observation yet — all A01–A16/P01–P26 remain pending by rule.
3. No gameplay certification claimed anywhere.

## Decisions

- M0 branch `migration/m0-baseline` created from `main`; user `docs/README.md` index edit preserved uncommitted.
- `docs/migration/` ledger directory created by this migration (new durable artifacts per plan §14).
- Integrator rule (plan §7): shared Engine/Strategy/AiFactory/host changes go through one owner (this session) once M2 starts.
- Class implementers may parallelize only after M2/M3 stabilize; not yet.

## Exact next action

1. Commit M0 ledgers on `migration/m0-baseline`; open draft M0 PR (no merge without authorization), link here.
2. Start M1 on `migration/m1-diagnostics` (stacked if M0 unmerged): decision telemetry + deterministic engine harness.
3. Real-client scenarios A01–A16 stay pending until user playtest; provide exact steps at M4/M5.
