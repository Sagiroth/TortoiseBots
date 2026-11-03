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

> Work in progress. These features are not officially released and not supported yet. Enable them at your own risk. Until the official release, bug reports and feedback about them are not handled through the issue tracker; contact Hypeer directly.

Decision capture answers "why did the run leader do that" and turns the answer into a test. While it is on, every tick of a running [dungeon run leader](../play/dungeon-run-leader.md) writes one JSON line holding everything the tick read (the whole facts, the whole run before the tick) and everything it decided (the order, the run after the tick). A live failure becomes a fixture: capture the walk, cut the window out of the file, write the decision the run leader should have made, fix the code, and the fixture goes from red to green.

It is **off by default** (`AiPlayerbot.DecisionCapture.Enabled = 0`) and costs nothing while off. It writes 12-40 KB per tick, roughly 45-145 MB per hour per run, so the shipped recommendation is test servers only. Player-led runs (`.bot run start ... player`) are not captured; bot-led runs are.

## Configuration

| Key | Default | Meaning |
| :--- | :--- | :--- |
| `AiPlayerbot.DecisionCapture.Enabled` | `0` | Capture the run leader's ticks. |
| `AiPlayerbot.DecisionCapture.Dir` | `decisions` | Output folder; relative paths are under `LogsDir` (an absolute path stands as written). |
| `AiPlayerbot.DecisionCapture.SegmentMiB` | `32` | A segment rotates past this many MiB (clamped to 1..256). |
| `AiPlayerbot.DecisionCapture.CaptureMiB` | `512` | Segments of one capture past this many MiB are deleted oldest first, marked `deleted` in the index; never the segment being written (clamped to `SegmentMiB`..4096). |
| `AiPlayerbot.DecisionCapture.TotalMiB` | `4096` | The folder is measured once, at server start, on a background thread; after that every capture's bytes come from its segment writer and its finished index, so the sweep never walks the folder on the world thread. Past this many MiB the oldest ended captures are deleted, and only a folder whose `index.json` names it, carries this format version, a valid end time and a finished status (`complete`, `incomplete`, `failed` or `stopped`); a foreign folder, a malformed index or a symlink is never touched. If the running captures alone are over it, the largest one is stopped (`endReason` `stopped: total cap`) and its files are kept; a running capture's files are never deleted (clamped to `CaptureMiB`..65536). |
| `AiPlayerbot.DecisionCapture.RecordKiB` | `512` | A record larger than this is written as a stub and the next tick is a keyframe (clamped to 64..4096). |
| `AiPlayerbot.DecisionCapture.KeyframeEvery` | `60` | A full keyframe every this many records (clamped to 1..600), always at a segment start. |

The keys are read when the configuration is loaded; a `.reload config` does not change a capture.

### What `TotalMiB` measures

The capture folder is measured exactly once, at server start, on a background thread. After that the total is the sum of the bytes every capture's segment writer accepted (for a finished capture, the numbers in its final `index.json`), so the sweep never walks the tree on the world thread. Two consequences are accepted for now: a file something else puts into the folder while the server runs is not counted until the next restart, and a running capture's counted bytes can trail the file size on disk by one writer queue. Retention deletes only what a folder's own `index.json` proves is a finished capture of this format.

### Deploying it on the test servers

The capture is off in the shipped template. On `m910q` and `tortoise-test2`, with `LogsDir` being the core's own key in `mangosd.conf` (shown so the path is unambiguous):

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

That writes `/opt/turtle/logs/decisions/<captureId>/` (substitute the server's own `LogsDir`); an absolute `AiPlayerbot.DecisionCapture.Dir` works the same way and is what to use when the dashboard runs in a container with its own mounts. The caps keep about a night of three two-hour walks (roughly 900 MB) before anything is trimmed; raise `TotalMiB` if the folder is on a small disk and lower `CaptureMiB` to keep fewer segments per capture.

The dashboard serves the folder over its normal login:

- Host install: `TBDASH_DECISIONS_DIR=/opt/turtle/logs/decisions ./tools/observability/run-dashboard.sh` (the script passes the variable through). The daemon's flag is `--decisions-dir DIR`, env `TBDASH_DECISIONS_DIR`.
- Container: mount the capture folder read-only at a path inside the container and name that path in the run command:
  `docker run -d -p 8095:8095 -v /opt/turtle/logs/decisions:/logs/decisions:ro -e TBDASH_DECISIONS_DIR=/logs/decisions <image>`
  (or append `--decisions-dir /logs/decisions` after the image; the image's entrypoint is the daemon). Without the mount the container cannot see the files; with the mount but no variable or flag the routes answer 404 `{"error":"decision capture folder not configured"}`.

## Files

Under `LogsDir` and `AiPlayerbot.DecisionCapture.Dir`, one folder per capture:

```text
decisions/<captureId>/index.json
decisions/<captureId>/seg-0001.jsonl
decisions/<captureId>/seg-0002.jsonl
```

The capture id is `<UTC yyyymmddThhmmssZ>-<plan>-<commander>`: the plan name and the commander name reduced to letters, digits and `_` (anything else becomes `_`, the plan cut at 48 characters and the commander at 24), with `-2`, `-3` ... appended when that folder already exists. A capture starts with `.bot run start` and ends when the run's slot goes away (`.bot run stop`, a time limit, an unbind, a shutdown); the id is named on the start line in the server log.

`index.json` is rewritten atomically at capture open, at every segment open and at the end, never per tick:

```json
{"format":"tb-decision-capture","v":1,"id":"20261102T161939Z-zul_gurub-Fionaa",
 "commander":"Fionaa","plan":"zul_gurub","map":309,
 "started":"2026-11-02T16:19:39Z","startedUnixMs":0,"ended":null,"endReason":"",
 "status":"running","schema":"89abf76bb51823a4",
 "segments":[{"file":"seg-0001.jsonl","firstSeq":1,"lastSeq":1800,"records":1800,"bytes":33554000,"deleted":false}],
 "dropped":0}
```

`status` is `running` while the capture runs, `incomplete` from the moment it ends until every segment writer has flushed, then `complete`; a writer that failed makes it `failed` and the failure is logged once. The final rewrite carries the bytes and records that actually reached the disk, and `dropped` counts the records lost on the way. A shutdown that does not wait for a busy writer leaves `incomplete` (or `failed`), never a false `complete`. The open segment's `lastSeq`, `records` and `bytes` are what was known at the last index write.

## Records

One JSON object per line, `tb-decision-capture` version 1. `seq` counts ticks of the capture from 1 and never resets across segments.

```text
{"k":"hdr","v":1,"schema":"<16 hex>","capture":"<id>","seg":3,"module":"<version>","commander":"Fionaa",
 "startedUnixMs":...,"settings":{...},"plan":{"name":...,"map":...,"hash":...,"body":{...}},
 "tactics":{"set":true,"rows":[...]}}
{"k":"tick","seq":1242,"t":<unix ms>,"dt":1000,"key":false,
 "facts":{<changed facts fields, or all when key>},
 "run":{<run fields the glue wrote since the last post-tick run, or all when key>},
 "out":{"order":"...","state":0,"phase":4,"step":322,"note":"...","say":""},
 "post":{<run fields this tick changed>},"capUs":412}
{"k":"stub","seq":1243,"t":...,"bytes":612345}
{"k":"gap","seq":1250,"dropped":3}
{"k":"end","t":...,"reason":"stopped","records":...,"dropped":...}
```

- `hdr` opens every segment and carries the schema hash, the whole plan (by value) and the boss tactic rows, so a replay needs no database and no plan JSON.
- `tick` carries only the top-level fields that changed; a keyframe (`"key":true`) carries all of them. `run` holds what the glue wrote between ticks (stand searches, refusals, pause/resume orders), `post` what the tick itself changed.
- `stub` replaces a record over `RecordKiB`; the next tick is a keyframe.
- `gap` says how many lines the disk could not take; the next tick is a keyframe.
- `end` closes the capture (`reason` is `slot released`, `all slots released`, `server shutdown` ...).

`capUs` is the time the tick spent encoding its two snapshots (the budget is 2 ms at `-O2`). Floats round-trip exactly (`float` `%.9g`, `double` `%.17g`, `nan`/`inf` as strings), integers are exact up to `uint64`, sets and maps are written sorted.

A schema change (a field added, removed, retyped or reordered, an enumerator moved) changes the schema hash in every header; an old capture or fixture then fails with a field diff instead of replaying wrongly.

## Privacy

A captured struct holds names, guids, entries, positions and the run's own text only. The header holds the capture id, the commander name, the plan, the module version, the schema and the seven `DecisionCapture.*` settings. No configuration value, account, address or credential is written, and the module's codec test pins the captured field names.

## Sizes

A 20-bot Zul'Gurub tick in a fight is about 20 member facts and up to 30 unit facts: a facts keyframe of 15-40 KB, a run keyframe of about 20 KB, deltas smaller. At 1 Hz that is 45-145 MB per hour per run, at most about 300 MB for a two-hour walk and about 900 MB for a night of three walks before retention trims anything. `capUs`, the bytes per record and `dropped` in `index.json` are the numbers to watch on the first live walk.

## Pulling a capture

- **Dashboard** (when a dashboard is configured): `GET /api/v1/decisions?commander=<name>&since=<RFC3339>&until=<RFC3339>` lists the captures and `GET /api/v1/decisions/<id>/<file>` serves `index.json` or a segment, behind the usual login.
- **turtle-agent**: a walk copies its own character's captures into `<out>/decisions/<id>/` after the teardown step, or `turtle-agent decisions pull --commander <name> --out <dir>` for a walk that died early.

## A failure becomes a fixture

1. Find the moment: `replay_decisions find decisions/<id>/seg-*.jsonl --step 315 --order-contains pull` prints the `seq` of the pull tick; `show <segments> --seq N --facts` shows what the run leader saw.
2. Cut the window: `replay_decisions extract <segments> --from N-20 --to N+5 --name zg-g12-pull --out-dir tools/fixtures/decisions` writes the fixture with status `open` and no expectations.
3. Write the decision the run leader should have made into `expect` (for example `{"seq":N,"orderNot":"<the recorded pull order>","why":"G15 is 19.5 yd from G12: assistance at 20 yd joins it"}`). `run tools/fixtures/decisions/*.json` shows XFAIL while the bug stands.
4. Fix the code and flip the fixture to `status: fixed`; CI now fails if the bug comes back, and fails on an `open` fixture that starts passing (flip it, do not leave it).

`replay_decisions why <fixture> --seq N` answers "which facts drove it": it replays the tick once per facts field reset to its default and lists the ones that change the order, phase or step. Fixtures and plan files are limited (300 ticks and 1 MiB per fixture, 4 MiB per plan file).
