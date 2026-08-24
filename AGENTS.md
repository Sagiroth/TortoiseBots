# AGENTS.md

## Project

This workspace is for PlayerBots development targeting **Tortoise WoW 1.18.1**.

This repository is available online (private) at <https://github.com/tortoise-wow-stack/TortoiseBots> — but this AGENTS.md is intentionally agnostic to where the checkout lives. Before pulling/cloning anything, check whether a local copy already exists alongside this repository; only use the online URL if no local checkout is present.

PlayerBots is intentionally being rebuilt around a clean, optional module
architecture.

The Tortoise core must remain usable without PlayerBots.

The primary architectural objective is:

> Harvest mature PlayerBots behavior without inheriting mature PlayerBots
> coupling.

---

## Read first

For PlayerBots work, read in this order:

1. `docs/PLAN.md` (canonical online copy: <https://github.com/tortoise-wow-stack/TortoiseBots/blob/main/docs/PLAN.md> — check for a local copy first; it may already exist alongside this repository)
2. `docs/HOST_API.md` if it exists
3. The relevant files under `docs/`
4. `docs/DISCOVERY.md` only when historical/upstream context is needed

Do not use `DISCOVERY.md` as the active implementation plan. `docs/PLAN.md` is the
execution source of truth.

When researching existing PlayerBots behavior, also read the
Tortoise WoW Knowledge Base instructions first:

- First check whether a local checkout already exists alongside this repository (look for a sibling `TortoiseWoWKnowledgeBase` checkout).
- Only if no local copy is present, use the online reference: <https://github.com/tortoise-wow-stack/TortoiseWoWKnowledgeBase/blob/main/AGENTS.md>

Then use its `playerbots/` documentation as a behavioral/capability reference.

---

## Reference repositories (check local first — then online)

This repository is intentionally agnostic to absolute filesystem paths.

**Before cloning/pulling anything, check whether a local read-only checkout already exists alongside this repository.** Common sibling locations to look for (names may vary, prefer relative discovery):

- `playerbots-references/` (expected to contain `mangoszero-server`, `cmangos-playerbots`, `cmangos-mangos-classic`, `shyalya-tortoise-wow`)
- `TortoiseWoWKnowledgeBase` (sibling checkout)
- `tortoise-docker-penqle` (local Docker/runtime environment)

Only if no local copy is found, use the online URLs below as the reference point. Do not hardcode absolute paths like `/mnt/...` — they vary per machine.

Online references (use only if local not present):

- Knowledge Base: <https://github.com/tortoise-wow-stack/TortoiseWoWKnowledgeBase>
- `mangoszero-server`: <https://github.com/mangoszero/server>
- `cmangos-playerbots`: <https://github.com/cmangos/playerbots>
- `cmangos-mangos-classic`: <https://github.com/cmangos/mangos-classic>
- `shyalya-tortoise-wow`: <https://github.com/Shyalya/tortoise-wow>
- This project (private): <https://github.com/tortoise-wow-stack/TortoiseBots>
- Penqle core: <https://github.com/Penqle/tortoise-wow>
- Docker/runtime environment: <https://github.com/Sagiroth/tortoise-docker-penqle> (local sibling `tortoise-docker-penqle` if present)

Local Penqle Docker/runtime environment (if present — check sibling checkout first):

### Docker/runtime environment purpose

Use `tortoise-docker-penqle` as the preferred local environment for:

- building/running the Penqle server when that workflow is already supported there
- starting/stopping the local server stack
- runtime verification
- server log inspection
- database-backed behavior checks
- reproducing login/session/world behavior
- validating PlayerBots changes in a realistic local environment

This Docker workspace is **not** an upstream PlayerBots reference repository.

Treat it as the local execution/validation environment for the target core.

### Docker workspace safety

Unless the current task explicitly requires Docker/infrastructure changes:

- do not edit Dockerfiles
- do not edit compose files
- do not change database initialization scripts
- do not rebuild/reset databases destructively
- do not remove volumes
- do not wipe containers/images/volumes
- do not alter unrelated runtime configuration

Prefer using the existing documented Docker workflow as-is.

Before running destructive Docker or database operations, stop and explain why
they are required.

Do not use commands equivalent to:

```text
docker compose down -v
docker volume rm ...
docker system prune ...
DROP DATABASE ...
TRUNCATE ...
```

unless explicitly requested by the user.

If the Docker environment contains its own `AGENTS.md`, `README`, or operational
instructions, read those before using it.

When validating PlayerBots behavior, prefer this environment over inventing a
new ad-hoc runtime setup.

### Reference repositories are read-only

These repositories are research material.

Do not:

- edit them
- commit to them
- rebase/reset them as part of implementation work
- treat them as part of the target PlayerBots implementation
- blindly copy their architecture into Tortoise
- assume a local checkout is the implementation target

Read/search/diff/history inspection is allowed.

If a reference checkout is missing, do not fabricate its contents. If network
access is available, the upstream repositories listed in `PLAN.md` may be used
instead.

Before relying on a reference implementation for provenance, record the exact
commit:

```bash
git -C "<reference-path>" rev-parse HEAD
```

---

## What each reference is for

### TortoiseWoWKnowledgeBase

Use primarily as:

- behavioral specification
- capability inventory
- public command/reference behavior
- ownership/security/failure reference
- research index
- acceptance-test inspiration

Read its `AGENTS.md` before using its PlayerBots material.

The Knowledge Base describes **what should happen**. It does not dictate the
new module architecture.

### `shyalya-tortoise-wow`

Use primarily as:

- Turtle WoW 1.18.1 compatibility evidence
- known host/API incompatibilities
- Turtle-specific fixes
- talent/spell differences
- session/movement/group/loot lessons
- evidence of which integration points became painful

Do not reproduce its broad host-hook surface automatically.

### `cmangos-playerbots`

Use primarily as:

- mature combat behavior
- movement behavior
- class strategies
- healing/CC/interrupt logic
- dungeon/raid behavior
- accumulated bug fixes and edge cases

Treat it as the richest behavior source, not as the architecture to transplant.

### `cmangos-mangos-classic`

Use when understanding what `cmangos-playerbots` expects from its host core:

- API definitions
- lifecycle behavior
- session/player semantics
- movement/group/map APIs
- compile-time integration points

Do not assume CMaNGOS host APIs should be recreated in Tortoise.

### `mangoszero-server`

Use primarily as:

- MaNGOS-Zero-family lifecycle reference
- bot character creation/login ideas
- session handling
- group handling
- random-bot lifecycle
- smaller/native bot-system patterns

It is a reference implementation, not proof that its module boundary is the
right one for Tortoise.

---

## Reference lookup strategy

Do not search every repository for every task.

Choose references based on the question.

### Public behavior / commands / ownership

Start with:

1. Knowledge Base
2. Shyalya if Turtle-specific behavior matters
3. CMaNGOS PlayerBots if implementation detail is needed

### Combat / class AI / healing / CC / movement

Start with:

1. Knowledge Base for expected behavior where documented
2. CMaNGOS PlayerBots for mature implementation
3. Shyalya for Turtle-specific differences/fixes
4. MangosZero only when it provides a useful alternative

### Session / lifecycle / bot login

Start with:

1. Current Tortoise core
2. MangosZero
3. Shyalya
4. CMaNGOS PlayerBots + CMaNGOS Classic only as additional references

The current Tortoise architecture always has priority over making an upstream
port easier.

### Turtle-specific spells / talents / custom content

Start with:

1. current Tortoise server data/code
2. Knowledge Base
3. Shyalya
4. CMaNGOS/Vanilla references only for comparison

Do not assume Vanilla 1.12 behavior is correct for Turtle 1.18.1.

### Runtime / server behavior / integration failures

Start with:

1. current target source tree
2. `tortoise-docker-penqle` Docker/runtime environment (sibling checkout, if present — online ref: <https://github.com/Sagiroth/tortoise-docker-penqle>)
3. server logs / runtime state
4. reference repositories only if the failure needs comparison

Use the Docker environment to answer questions such as:

- does the server start?
- does the module load?
- can a headless session log in?
- does the character enter the world?
- does logout/save/reconnect work?
- does the database contain the expected runtime state?
- does a behavior work in-game rather than only compile?

Static inspection is not a substitute for runtime verification when the task
depends on actual server behavior.

---

## Architecture invariants

These rules are non-negotiable.

### Keep PlayerBots optional

`BUILD_PLAYERBOTS=OFF` must remain a supported first-class build.

The core must build without the PlayerBots module checkout.

No PlayerBots runtime/config/SQL dependency may be required when bots are off.

### Do not reintroduce legacy coupling

Never recreate patterns such as:

- `WorldSession::GetBot()`
- `WorldSession::SetBot()`
- `m_bot`
- `sPlayerBotMgr`
- `PlayerBotEntry` inside normal core gameplay code
- scattered `if (IsBot())` / `if (GetBot())` checks

Normal gameplay systems should not need to know that a `Player` is controlled
by a bot.

**If implementing a PlayerBots feature requires adding bot-specific conditions
to unrelated core gameplay systems, stop and explain why before proceeding.**

### Prefer module-only changes

Before modifying core code, determine:

1. Can this be implemented entirely inside the module?
2. Does an existing ScriptMgr/event/lifecycle hook already expose what is needed?
3. Can the missing capability be expressed as a generic core concept rather
   than a PlayerBots-specific concept?

Only add a new core seam when the existing core cannot provide the required
capability cleanly.

### Centralize host integration

Any unavoidable PlayerBots/core integration must stay concentrated in the
small approved host boundary.

Do not spread PlayerBots hooks through `Player.cpp`, `Unit.cpp`, `Spell.cpp`,
`WorldSession.cpp`, movement, groups, etc.

Aim for roughly:

```text
<= 5 directly PlayerBots-aware core files where practical
```

If the host integration starts growing toward roughly 8–10 files before the
MVP works, treat that as an architectural warning and reassess.

### Headless sessions

Treat bot sessions as a session/transport concern.

Prefer concepts such as:

- network-backed session
- headless session
- `HasNetworkTransport()`
- `CanReceiveClientPackets()`

Do not make generic core code ask whether a session belongs to a bot.

The core may understand a generic headless/non-network session capability.

The PlayerBots module should understand that a particular headless session is
being used to control a bot.

### LLM isolation

LLMs must never be required for:

- combat
- movement
- healing
- threat
- interrupts
- CC timing
- any real-time game-loop decision

LLM integration must be asynchronous and optional.

If the LLM/network service is unavailable, normal bot gameplay must continue.

---

## Upstream PlayerBots rule

CMaNGOS PlayerBots, MangosZero, Shyalya/r-o-sh, older PlayerBots
implementations, and the Knowledge Base are reference material.

General rule:

```text
harvest behavior, not architecture
```

Do not vendor an upstream PlayerBots tree into the Tortoise core.

Do not blindly cherry-pick commits that introduce upstream host coupling.

Do not preserve an upstream class/module structure merely because it makes
copying easier.

For imported behavior:

1. understand the observable behavior
2. check the Knowledge Base where applicable
3. inspect the most relevant upstream implementation
4. inspect Turtle-specific differences where applicable
5. define the expected behavior / acceptance test
6. implement or port it inside the new module
7. test it
8. record provenance

Prefer:

```text
study -> extract intent -> port/reimplement -> test
```

over a literal cherry-pick when upstream dependencies or architecture differ.

A cherry-pick is acceptable only when the commit is isolated, compatible,
properly licensed/attributed, and does not expand core coupling.

---

## Provenance

Substantial copied or ported behavior must be recorded in:

```text
docs/PROVENANCE.md
```

Record at least:

```text
Feature:
Source repository:
Source commit:
Source files:
Copied / ported / independently reimplemented:
Reason:
Local validation:
```

Preserve required upstream license/copyright notices.

Do not silently copy large bodies of upstream or AI-generated code.

---

## Scope discipline

Prefer small vertical slices.

Do not:

- create speculative abstractions for future features
- implement every class at once
- start raid/BG/random-bot systems before the dungeon MVP
- refactor unrelated core code while implementing PlayerBots
- optimize for 1000 bots before the small-party use case works
- add compatibility layers merely to make an upstream code drop compile

The first useful target is:

```text
human + owned bot
```

Then:

```text
2 humans + bots filling a 5-player dungeon group
```

---

## Performance rules

From the beginning:

- no database query every bot tick
- no full-world scan every bot tick
- no synchronous external network calls in game/map threads
- no rebuilding large strategy graphs every update
- cache immutable spell/talent metadata where practical
- use event-driven invalidation where appropriate
- keep expensive diagnostics opt-in

When performance work begins, measure rather than guess.

Useful metrics include:

- bot update time
- total bot CPU
- DB queries
- path/movement requests
- AI decisions/sec
- memory per bot

---

## Local changes and Git safety

Never reset, clean, overwrite, stage, or include unrelated user changes.

Inspect:

```bash
git status --short
```

before and after work.

Do not use destructive Git commands unless explicitly requested.

Do not modify the local read-only reference repositories while implementing
the target module.

---

## Validation

Use the **smallest check that proves the current change**. Validation must not dominate implementation time.

Batch related edits before compiling. Do not rebuild after every file edit, every diagnostic observation, every docs change, or every manual test.

### Default development loop

For an active implementation slice:

```text
inspect
-> make a coherent batch of edits
-> one cached BUILD_PLAYERBOTS=ON build if compiled code changed
-> run the smallest relevant runtime/manual check
-> continue implementing
```

Repeat the build only if compiled code changed after the last successful build.

A successful build remains valid evidence for unchanged code. A successful manual test remains valid evidence for unchanged behavior.

### Validation cadence

- **Docs/comments/config-only change:** no C++ build. Run only the smallest relevant text/config check plus `git diff --check`.
- **Module-only C++ change:** run one cached `BUILD_PLAYERBOTS=ON` build of the smallest affected target after the edit batch is coherent.
- **Generic core-seam change:** while iterating, build the cached `BUILD_PLAYERBOTS=ON` target only. Do **not** run OFF/ON after every core edit.
- **Optional-build/CMake/build-gating change:** run the directly affected configuration when needed to prove the change, then defer the full matrix until the slice is stable.
- **Phase/PR/handover boundary:** run the full OFF/ON matrix **once**, after implementation has stabilized, plus coupling audits, `git diff --check`, and the required runtime gates.
- Use cached builds by default. Use `--no-cache` only to diagnose a cache/image problem or when explicitly requested.
- Rebuild the Docker image only when the runtime image genuinely needs new build output or build inputs changed. If the existing runtime can use the already-built binary, reuse it.
- If the binary/image is unchanged, restart or reuse the existing stack instead of rebuilding.
- If the user already manually validated a behavior and no code affecting that behavior changed afterward, **do not ask them to repeat the same test**.
- Do not repeat login/logout/follow/shutdown acceptance scenarios merely for ceremony. Re-run them only when the changed code could plausibly regress them or at the final phase/PR gate.

### Anti-churn rule

If validation/recompilation is taking more time than implementing the current playable slice:

1. stop,
2. identify the single cheapest check that proves the current edit,
3. run that check only,
4. continue implementation.

Do not turn every intermediate edit into a release-candidate validation cycle.

Every code change still needs a targeted check and a clear report of what was, and was not, run.

Useful audit:

```bash
rg -n \
  'GetBot\(\)|SetBot\(|\bm_bot\b|sPlayerBotMgr|PlayerBotEntry|PB_STATE_' \
  src
```

Any result must be investigated.

Also audit the approved host boundary periodically:

```bash
rg -n -i \
  'PlayerBot|BotService|BotSession|HeadlessSession' \
  src/game
```

Every result in normal core code must be explainable.

A growing number of unrelated matches is an architecture regression.

Do not delete legitimate:

- `PlayerAI`
- `PlayerControlledAI`
- Discord bot code
- gameplay entities/items/spells whose names naturally contain "Bot"
- anticheat references to generic botting

---

## Testing behavior

Prefer deterministic validation/debug scenarios.

Important areas include:

- headless login
- logout
- duplicate login
- human reconnect
- player save/reload
- server shutdown
- follow
- movement
- target selection
- threat
- healing
- interrupts
- CC
- death
- wipe recovery
- teleport/map transitions
- dungeon regrouping

Never claim behavior was tested if it was only inspected statically.

### Runtime validation with the local Penqle Docker stack

When a change affects runtime behavior and the Docker/runtime environment is available
(a sibling `tortoise-docker-penqle` checkout may already exist alongside this repository),
use that sibling checkout for the **smallest relevant verification**. For compilation/runtime
validation, use the persistent builder provided by the sibling `tortoise-docker-penqle`
checkout. Do not fall back to full Docker image rebuilds for normal source edits. Do not
rebuild merely to restart an unchanged binary; reuse the running image and preserve its data.

Prefer one focused gameplay check for the behavior just changed. Do not replay the whole
historical acceptance matrix after each slice. Full regression journeys belong at phase/PR
boundaries or after changes to the shared lifecycle/session seam.

Before running it:

1. read any Docker workspace instructions
2. inspect current container state
3. avoid destructive resets
4. preserve existing data unless the test explicitly requires otherwise

Record the actual commands used and the observed result.

Examples of runtime evidence:

- server starts successfully
- PlayerBots module loads
- bot character logs in
- bot enters world
- bot follows/acts
- bot logs out cleanly
- human reconnect works
- server shutdown is clean
- no unexpected DB/session errors appear in logs

Never report "runtime tested" when only compilation or static inspection was
performed.

---

## Reporting

At the end of each PlayerBots task report:

- files changed
- core files changed
- module files changed
- new host hooks
- why each core hook was necessary
- tests/builds performed
- Docker/runtime validation performed
- observed behavior
- relevant server/log evidence
- remaining issues
- architecture concerns
- provenance for imported behavior

If no build/test was run, say so explicitly.

If a requested feature would violate an architecture invariant, stop and
explain the conflict rather than quietly weakening the boundary.
