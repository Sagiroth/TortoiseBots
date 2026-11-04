# Decision fixtures

A fixture is a window of run-leader ticks cut out of a decision capture
(`docs/server/decision-capture.md`): the `Facts` and `Run` the leader read and
decided on, plus the plan it ran. `replay_decisions run` rebuilds both structs,
calls the real `DungeonRun::Tick` and compares the order and the whole
post-tick `Run` field by field.

## Files

```
synthetic-pull.json          the committed end-to-end fixture (tools/DecisionReplayScenario.h)
plans/<hash>.json            {"plan": <encoded plan>, "tactics": {"set":..,"rows":[..]}}
schemas/<hash>.txt           the field list the fixture was captured with
```

`plans/` is content-addressed: every fixture of the same plan version shares the
file. `schemas/` holds the old field list while a fixture is behind the current
schema; it is only needed to print the field diff and to upgrade.

## Fixture

```json
{"format":"tb-decision-fixture","v":1,"name":"synthetic-pull","schema":"<16 hex>",
 "planHash":"<16 hex>","plan":"plans/<hash>.json","mode":"independent",
 "reproduce":"before-first-mark","status":"fixed",
 "source":{"capture":"<id>","segment":"seg-0001.jsonl","fromSeq":1,"toSeq":40,"walk":"","issue":""},
 "expect":[{"seq":22,"orderPrefix":"pull ","why":"the decision the fix should make"}],
 "ticks":[{"k":"tick","seq":1,"t":0,"dt":1000,"key":true,"facts":{...},"run":{...},
           "out":{"order":..,"state":..,"phase":..,"step":..,"note":..,"say":..},"post":{...}}],
 "history":[]}
```

- `mode`: `independent` (each tick from its own captured pre-tick `Run`) or
  `chain` (the replayed `Run` carried on, with the next record's `run` delta —
  the glue's writes — laid over it).
- `reproduce`: `all`, `before-first-mark` (default; every tick before the first
  expectation's seq, or every tick when there is no expectation) or `none`.
- `status`: `open` (a failure is XFAIL and CI green; passing is XPASS and CI
  red with "flip status to fixed") or `fixed` (a failure is CI red).
- `expect` checks the replayed tick: `order`, `orderPrefix`, `orderNot`,
  `orderContains`, `phase` (name), `noteContains`, `step`, and `run` (field ->
  JSON; compared byte for byte with the replayed post-tick field). `why` is the
  explanation printed with a failure.
- `ticks`: the record grammar's `tick` object. `facts`/`run` are deltas against
  the previous record (all fields when `key`), `post` is a delta against the
  record's own pre-tick `Run`.
- `history`: what `upgrade` changed, newest last.

## Commands

```sh
g++ -std=c++17 -O2 -Wall -Wextra tools/replay_decisions.cpp -o /tmp/replay_decisions

/tmp/replay_decisions list decisions/<id>/seg-*.jsonl
/tmp/replay_decisions find decisions/<id>/seg-*.jsonl --step 315 --order-contains pull
/tmp/replay_decisions show decisions/<id>/seg-*.jsonl --seq 1242 --facts
/tmp/replay_decisions extract decisions/<id>/seg-*.jsonl --from 1222 --to 1247 \
    --name zg-arlokk-g12-pull --out-dir tools/fixtures/decisions
/tmp/replay_decisions run tools/fixtures/decisions/*.json --verbose
/tmp/replay_decisions why tools/fixtures/decisions/<name>.json --seq 1242
/tmp/replay_decisions upgrade tools/fixtures/decisions/<name>.json
```

`extract` writes `status: open` and no expectations: land it with the bug, then
write the decision the fix should make into `expect` (red, XFAIL) and flip the
fixture to `fixed` with the fix. `upgrade` accepts added fields (they decode to
the struct default), drops removed fields, and refuses retyped fields and enums
whose existing enumerators moved; it records the step in `history` and checks
that the upgraded fixture still reproduces. An old fixture whose
`schemas/<hash>.txt` is gone cannot be diffed or upgraded.

`synth --out-dir tools/fixtures/decisions` regenerates `synthetic-pull.json`
(and its plan and schema files) from `tools/DecisionReplayScenario.h` with
`status: fixed`; the fixture is the recorder, extractor and replay checked
end to end. `tools/test_decision_replay.cpp` runs every committed fixture and
enforces the caps (300 ticks, 1 MiB per fixture, 4 MiB per plan) and scans
every string for credentials.
