# TortoiseBots migration — operator/player runbook (M10)

## Branches and PRs (all draft, DO NOT MERGE without explicit authorization)

| Milestone | Branch (stacked) | PR |
| --- | --- | --- |
| M0 baseline | `migration/m0-baseline` (from `main`) | #70 → `main` |
| M1 diagnostics | `migration/m1-diagnostics` | #71 → M0 |
| M2 engine | `migration/m2-engine` | #72 → M1 |
| M3 spells | `migration/m3-spells` | #73 → M2 |
| M4 party | `migration/m4-party` | #74 → M3 |
| M5 slices | `migration/m5-slices` | #75 → M4 |
| M6 classes | `migration/m6-classes` | #76 → M5 |
| M7 world | `migration/m7-world` | #77 → M6 |
| M8 content | `migration/m8-content` | #78 → M7 |
| M9 services | `migration/m9-services` | #79 → M8 |
| M10 closure | `migration/m10-closure` | #80 → M9 |

Merge order after authorization: #70, then rebase #71 onto `main`, repeat
down the stack. Each PR description carries scope/validation/pending gates.

## Build and guard gates (operator runs these; agent could not)

```bash
# read-only gates (no server needed)
bash tools/verify_turtle_surface.sh
bash tools/verify_penqle_host_contract.sh --core /explicit/path/to/tortoise-wow
python3 tools/check_decision_trail.py --self-test
# module build matrix per docs/MERGE_ACCEPTANCE.md (ON/OFF/absent)
```

## Enabling the decision trail (disposable fixture only)

1. Set `AiPlayerbot.EnableActionLog=1`, restart, summon a disposable bot.
2. Exercise the scenario; collect `logs/bots/<bot>_*.log`.
3. `python3 tools/check_decision_trail.py <log> [--strict]`.
4. `--strict` nonzero or MALFORMED = emitter contract break; file it with the log.

## Real-client acceptance (user playtest, gates stay pending until done)

- Owned party: A01–A05 (login/follow/attack/reach/cast), A10–A13
  (pull/loot/death/teleport), A15 (talents/presets).
- Party tactics: A06–A09 (tank/heal/interrupt/CC), A14 (attrition), A16
  (service handoff).
- Journeys: P01–P08 (quest/world), P09–P13 (market), P14–P18
  (population/LFT/BG), P19–P26 (commands/presets/modes/soak).
- Dungeon: human + 4 bots with deliberate pulls, CC, interrupts, loot,
  wipe recovery (M5); Turtle maps + raids per M8 rows (P24).

## Service operation (all default-off; required scope even when off)

- Random population, LFT fill, AH market, BG queue: enable one at a time
  after owned-party acceptance; check cancel/retry/restart paths each.
- Market full-supply needs the M9 core seam first — do not enable expecting
  empty-market stock.
- Soak per the F25 measurement plan (stages × telemetry × services).

## Rollback

- Every milestone is its own branch; revert = drop the branch.
- Ledgers (`docs/migration/`) are additive; `tortoise_bots_owned_character`,
  `db_store`, `custom_strategy` rows are never dropped by migrations.
- `AiPlayerbot.WorldBuff` now loads real values: previously-inert keys take
  effect — review worldbuff config before deploying M10.
