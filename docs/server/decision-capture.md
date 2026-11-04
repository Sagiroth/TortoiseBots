---
id: server-decision-capture
title: Decision Capture (opt-in run-leader recorder)
category: server
summary: Record every run-leader decision with the facts and run state behind it, cut a live failure into a replay fixture, and keep it as a regression test.
tags: [guide, recorder, run-leader, testing, replay, fixtures]
relates_to:
  - server-configuration
  - server-run-trace
  - play-dungeon-run-leader
---

# Decision Capture

Decision capture answers "why did the run leader do that" and turns the answer into a test. While it is on, every tick of a running [dungeon run leader](../play/dungeon-run-leader.md) writes one JSON line:

- Everything the tick read: the facts, the run before the tick.
- Everything it decided: the order, the run after the tick.

A live failure becomes a fixture: capture the walk, cut the window out of the file, write the decision the run leader should have made, fix the code, and the fixture goes from red to green.

It is **off by default** (`AiPlayerbot.DecisionCapture.Enabled = 0`). Player-led runs (`.bot run start ... player`) are not captured; bot-led runs are. One record is about 12-40 KB at 1 Hz, so 45-145 MB per hour per run: test servers only.

## Configuration

| Key | Default | Meaning |
| :--- | :--- | :--- |
| `AiPlayerbot.DecisionCapture.Enabled` | `0` | Capture the run leader's ticks. |
| `AiPlayerbot.DecisionCapture.Dir` | `decisions` | Output folder. Relative paths sit under `LogsDir`; an absolute path stands as written. |
| `AiPlayerbot.DecisionCapture.SegmentMiB` | `32` | A segment rotates past this many MiB (1..256). |
| `AiPlayerbot.DecisionCapture.CaptureMiB` | `512` | One capture past this many MiB drops its oldest segments first, never the one being written. At least the segment size, at most 4096. |
| `AiPlayerbot.DecisionCapture.TotalMiB` | `4096` | The folder past this many MiB drops its oldest ended captures. A running capture is never dropped; if running captures alone pass the cap, the largest one is stopped and its files kept. At least the capture cap, at most 65536. |
| `AiPlayerbot.DecisionCapture.RecordKiB` | `512` | A record larger than this is written as a stub and the next tick is a keyframe (64..4096). |
| `AiPlayerbot.DecisionCapture.KeyframeEvery` | `60` | A full keyframe every this many records, always at a segment start (1..600). |

The keys are read when the configuration loads. `.reload config` does not change a capture.

### What `TotalMiB` measures

The capture folder is measured once, at server start, on a background thread. After that the total comes from each capture's writer counts (for a finished capture, its final `index.json`), so the sweep never walks the folder on the world thread. Two consequences:

- A file something else puts into the folder while the server runs is not counted until the next restart.
- A running capture's counted bytes can trail the file size on disk by one writer queue.

Retention deletes only what a folder's own `index.json` proves is a finished capture of this format (`complete`, `incomplete`, `failed` or `stopped`). A foreign folder, a malformed index, or a symlink is never touched. A folder without a finished `index.json` is kept and never deleted, but its bytes still count toward the cap.

### Deploying it on the test servers

The capture is off in the shipped template. To turn it on (with `LogsDir` being the core's own key in `mangosd.conf`):

```ini
# mangosd.conf: the core's log folder, whatever the server already uses.
LogsDir = "/opt/turtle/logs"

# aiplayerbot.conf: the capture on, under LogsDir/decisions, with the shipped caps.
AiPlayerbot.DecisionCapture.Enabled = 1
AiPlayerbot.DecisionCapture.Dir = decisions
AiPlayerbot.DecisionCapture.SegmentMiB = 32
AiPlayerbot.DecisionCapture.CaptureMiB = 512
AiPlayerbot.DecisionCapture.TotalMiB = 4096
AiPlayerbot.DecisionCapture.RecordKiB = 512
AiPlayerbot.DecisionCapture.KeyframeEvery = 60
```

That writes `/opt/turtle/logs/decisions/<captureId>/` (substitute the server's own `LogsDir`). An absolute `Dir` works the same way.

## Files

One folder per capture:

```text
decisions/<captureId>/index.json
decisions/<captureId>/seg-0001.jsonl
decisions/<captureId>/seg-0002.jsonl
```

- The capture id is `<UTC yyyymmddThhmmssZ>-<plan>-<commander>`. Plan and commander names keep letters, digits and `_` (anything else becomes `_`); the plan is cut at 48 characters, the commander at 24. `-2`, `-3` and so on are appended when the folder already exists.
- A capture starts with `.bot run start` and ends when the run's slot goes away (`.bot run stop`, a time limit, an unbind, a shutdown). The id is named on the start line in the server log.
- `index.json` is rewritten at capture open, at every segment open, and at the end, never per tick.

`status` values:

- `running` while the capture runs.
- `incomplete` from the moment it ends until every segment writer has flushed, then `complete`.
- `failed`: a writer failed, a rotation's next segment could not be opened, or an `index.json` write failed. The failure is logged once with the path and reason. A failed first write stops the capture before it starts and removes the folder again.
- `dropped` counts the records lost on the way. The final rewrite carries the bytes and records that actually reached the disk. The open segment's `lastSeq`, `records` and `bytes` are what was known at the last index write.
- A shutdown that does not wait for a busy writer leaves `incomplete` (or `failed`), never a false `complete`.

## Records

One JSON object per line, version 1. `seq` counts ticks of the capture from 1 and never resets across segments.

```text
{"k":"hdr","v":1,"schema":"<16 hex>","capture":"<id>","seg":3,"module":"<version>","commander":"Fionaa",
 "startedUnixMs":...,"settings":{...},"plan":{"name":...,"map":...,"hash":...,"body":{...}},
 "tactics":{"set":true,"rows":[...]}}
{"k":"tick","seq":1242,"t":<unix ms>,"dt":1000,"key":false,
 "facts":{<changed fields, or all when key>},
 "run":{<run fields written between ticks, or all when key>},
 "out":{"order":"...","state":0,"phase":4,"step":322,"note":"...","say":""},
 "post":{<run fields this tick changed>},"capUs":412}
{"k":"stub","seq":1243,"t":...,"bytes":612345}
{"k":"gap","seq":1250,"dropped":3}
{"k":"end","t":...,"reason":"stopped","records":...,"dropped":...}
```

- `hdr` opens every segment. It carries the schema hash, the whole plan (by value) and the boss tactic rows, so a replay needs no database and no plan file.
- `tick` carries only the top-level fields that changed. A keyframe (`"key":true`) carries all of them. `run` holds what was written between ticks (stand searches, refusals, pause/resume orders); `post` holds what the tick itself changed.
- `stub` replaces a record over `RecordKiB`; the next tick is a keyframe.
- `gap` says how many lines the disk could not take; the next tick is a keyframe.
- `end` closes the capture. Reasons include `slot released`, `all slots released`, and `server shutdown`.

`capUs` is the microseconds the tick spent encoding its two snapshots (budget 2 ms). Round-trip rules:

- Floats round-trip exactly (`float` `%.9g`, `double` `%.17g`; `nan`/`inf` as strings).
- Integers are exact up to `uint64`.
- Sets and maps are written sorted.

A schema change (a field added, removed, retyped or reordered, an enumerator moved) changes the schema hash in every header. An old capture or fixture then fails with a field diff instead of replaying wrongly.

Replays do not see liquid queries (a callback the generator leaves out); captures made before `d774d05ea` need `replay_decisions upgrade`.

## Privacy

A captured struct holds names, guids, entries, positions, and the run's own text only. The header holds the capture id, the commander name, the plan, the module version, the schema, and the `DecisionCapture.*` settings. No account, address, or credential is written.

## Sizes

A 20-bot Zul'Gurub tick in a fight is about 20 member facts and up to 30 unit facts: a facts keyframe of 15-40 KB plus a run keyframe of about 20 KB, deltas smaller. At 1 Hz:

- About 45-145 MB per hour per run.
- About 300 MB for a two-hour walk.
- About 900 MB for a night of three walks before retention trims anything.

`capUs`, the bytes per record, and `dropped` in `index.json` are the numbers to watch on the first live walk.

## Pulling a capture

- **Dashboard** (when a dashboard is configured with a decisions folder): `GET /api/v1/decisions?commander=<name>&since=<RFC3339>&until=<RFC3339>` lists the captures, and `GET /api/v1/decisions/<id>/<file>` serves `index.json` or a segment, behind the usual login.
- **External driver**: a walk copies its own character's captures into `<out>/decisions/<id>/`, or pulls them with a `decisions pull` command for a walk that died early.

## A failure becomes a fixture

1. Find the moment: `replay_decisions find decisions/<id>/seg-*.jsonl --step 315 --order-contains pull` prints the `seq` of the pull tick. `show <segments> --seq N --facts` shows what the run leader saw.
2. Cut the window: `replay_decisions extract <segments> --from N-20 --to N+5 --name zg-g12-pull --out-dir tools/fixtures/decisions` writes the fixture with status `open` and no expectations.
3. Write the decision the run leader should have made into `expect` (for example `{"seq":N,"orderNot":"<the recorded pull order>","why":"..."}`). Fixtures with status `open` report XFAIL while the bug stands.
4. Fix the code and flip the fixture to `status: fixed`. CI fails if the bug comes back, and fails on an `open` fixture that starts passing (flip it, do not leave it).

`replay_decisions why <fixture> --seq N` answers "which facts drove it": it replays the tick once per facts field reset to its default and lists the ones that change the order, phase or step. Limits: 300 ticks and 1 MiB per fixture, 4 MiB per plan file.
