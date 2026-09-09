# TortoiseBots migration — operator/player runbook (M10)

## Branches and PRs (all draft, DO NOT MERGE without explicit authorization)

CONSOLIDATED: one branch, one PR (stacked #70–#80 closed as superseded).

| Milestone | Branch | PR |
| --- | --- | --- |
| M0–M10 (all) | `migration/behavior-migration` (16 commits from `main`) | #83 → `main` |

Per-milestone history is preserved as individual commits; review per commit
or per milestone diff. The PR description carries scope/validation/gates.

## Build and guard gates (operator runs these; agent could not)

```bash
# read-only gates (no server needed)
bash tools/verify_tortoise_surface.sh
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
  wipe recovery (M5); Tortoise maps + raids per M8 rows (P24).

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
