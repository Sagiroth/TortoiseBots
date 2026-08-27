# TortoiseBots

TortoiseBots is an optional native PlayerBots module for
[Tortoise WoW 1.18.1](https://github.com/Penqle/tortoise-wow).

It brings existing PlayerBots (primarily AzerothCore/mod-playerbots) combat, movement, class, group, loot, quest and
travel behavior to Tortoise WoW while keeping bot-specific logic outside the
core.

> **Harvest behavior, not architecture.**

TortoiseBots uses the upstream native module system and a small generic host API
instead of recreating the old tightly coupled `GetBot()` / `m_bot` /
`sPlayerBotMgr` architecture.

---

## Status

> **WIP — local integration is complete; upstream core and module PRs are awaiting merge.**

- [x] Native TortoiseBots module implemented
- [x] Existing `PlayerbotAI` integrated
- [x] All nine Vanilla classes included
- [x] Vanilla/Turtle 1.18.1 cleanup and compatibility audit completed
- [x] Headless session lifecycle validated against the pinned baseline
- [x] PlayerBots-enabled and module-disabled builds validated
- [x] Local Docker build and startup against integrated `#396 + #411 + #416` core
- [x] TortoiseBots feature stack #37–#42 assembled
- [ ] Upstream core PRs #411 and #416 merged
- [ ] Manual owned-bot gameplay acceptance
- [ ] Manual 5-player dungeon acceptance

The local integration checkpoint is complete. Upstream merge status and exact
source revisions remain tracked in Git history and Docker `SOURCE_IDENTITY`.

---

## Core dependencies

TortoiseBots targets [tortoise-wow](https://github.com/Penqle/tortoise-wow) (`Penqle/tortoise-wow`) as its canonical upstream core.

### Generic Headless sessions

TortoiseBots needs the core to support a `WorldSession` without a network client.

At a high level the core needs to provide:

- Network and Headless session types
- character-GUID keyed Headless session management
- deferred Headless character login
- normal `World` ownership of Headless session lifetime
- generic player/world lifecycle hooks
- generic packet hooks usable by native modules

> [!IMPORTANT]
> **Requires upstream PR #411 until merged.**
> Build against [`tortoise-wow#411`](https://github.com/Penqle/tortoise-wow/pull/411)
> (`feature/headless-world-session`) or wait for `main` to include it.
> Plain `upstream/main` without #411 does not provide `SessionTransport::Headless`
> / GUID-keyed lifecycle and the module will fail to link.
> Exact pinned SHAs are in Git history (see below).
>
> TortoiseBots also uses PR #416 for generic character creation and the
> participant primitives used by the auto-create, LFT and BG features. PR #416
> is stacked on #411 and should merge after it.

The intended boundary is:

```text
Penqle/tortoise-wow
        |
        | generic module + Headless host API
        v
TortoiseBots
        |
        | bot lifecycle + AI + gameplay behavior
        v
PlayerbotAI
```

The core should expose **Headless sessions**, not PlayerBots concepts.

TortoiseBots should **not** require a bot-specific fork of the core.

Removal of the historical built-in PlayerBots subsystem
([PR #396](https://github.com/Penqle/tortoise-wow/pull/396)) is optional from
the TortoiseBots API perspective. It is recommended for a clean core because
it removes the obsolete runtime and schema dependency; TortoiseBots does not
use any code removed by that PR. The local Docker integration was tested with
#396 present.

---

## Features

- Same-account bots (`.bot add` / `remove`) with human reclaim
- Party: follow / stay / invite via `.bot` (group AI)
- Class AI for all 9 Vanilla classes — basic combat / heal / tank
- Loot, quest and travel / taxi handling
- Turtle Goblin / High Elf and Turtle spell / talent / mount handling where validated
- Bounded random-bot login and teleport for existing characters (TortoiseBots #37)
- Optional pinned random-bot pool (TortoiseBots #38)
- Optional RNDBOT auto-create (TortoiseBots #39)
- Optional LFT autofill (TortoiseBots #40)
- Optional AH market population (TortoiseBots #41)
- Optional BG autoqueue (TortoiseBots #42)

The stacked feature services are configuration-gated and remain off by default
unless their corresponding settings are enabled.

Targets Vanilla/Turtle 1.18.1 — Any future expansions elements are removed.

---

## Architecture

TortoiseBots is a native optional module, not a replacement core and not a
vendored PlayerBots core fork.

```text
Upstream / Tortoise core
        |
        | generic Headless / lifecycle / packet / command hooks
        v
TortoiseBots
        |
        +-- BotManager
        +-- PlayerbotAIAdapter
        +-- PlayerbotAIStorage
        +-- host adapters
        +-- Existing PlayerbotAI (primarily AzerothCore)
        +-- Strategy / Trigger / Action / Value
```

Normal core gameplay code should not need bot-specific state.

Legacy patterns such as these are intentionally avoided:

```cpp
WorldSession::GetBot()
WorldSession::SetBot()
m_bot
sPlayerBotMgr
PlayerBotEntry
```

---

## Session model

Owned bots are normal Tortoise characters using Headless `WorldSession`s.

```text
One account
    +-- at most one Network session
    +-- zero or more Headless character sessions
```

Network sessions remain account-keyed. Headless sessions are keyed by character GUID.

This allows a connected player and owned alternate characters to coexist
without fake account IDs or dedicated bot accounts.

The core owns `WorldSession` lifetime. TortoiseBots owns bot records and AI.

The detailed integration contract is documented in [`docs/HOST_API.md`](docs/HOST_API.md).

---

## Commands

The native command surface currently includes:

```text
.bot add
.bot remove
.bot follow
.bot invite
.bot uninvite
.bot stay
.bot list
.bot stats
.bot command
.bot help
```

`.bot command` forwards into the existing PlayerBots (primarily AzerothCore/mod-playerbots) command system for the
selected bot. Commands enforce normal account ownership or GM authority.

---

## Module layout

TortoiseBots is consumed as `tortoise-wow/modules/TortoiseBots/`:

```text
tortoise-wow/
└── modules/
    └── TortoiseBots/
```

Main directories:

```text
ai/          PlayerBots AI and Turtle compatibility
behavior/    Module-owned helpers
commands/    Native .bot commands
conf/        Module configuration
data/sql/    Module migrations
host/        Core <-> TortoiseBots boundary
runtime/     Bot lifecycle and AI ownership
src/         Native module entrypoint
tools/       Audit and development tooling
docs/        Architecture, status and provenance
```

---

## Build

Normal native module selection:

```text
BUILD_LEGACY_PLAYERBOTS=OFF
MODULES=static
MODULE_TORTOISEBOTS=static
```

`BUILD_LEGACY_PLAYERBOTS` refers to the historical built-in PlayerBots
implementation and should remain disabled when using TortoiseBots.

See [`AGENTS.md`](AGENTS.md) for development and validation workflow.

---

## Configuration and data

Configuration:

```text
conf/tortoise_bots.conf.dist
ai/playerbot/aiplayerbot.conf.dist.in
```

Database migrations:

```text
data/sql/world/
data/sql/char/
```

The inherited PlayerBots configuration is broader than the currently validated
Turtle gameplay surface. The existence of a setting does not by itself mean the
corresponding feature has completed gameplay acceptance.

The current stacked feature toggles include:

```text
AiPlayerbot.Enabled
AiPlayerbot.EnableRandomTeleports
AiPlayerbot.PinnedBots
AiPlayerbot.RandomBotAutoCreate
AiPlayerbot.RandomBotLftEnabled
AiPlayerbot.AhMarketEnabled
AiPlayerbot.RandomBotBgEnabled
```

For the Docker workflow, `AI_PLAYERBOT_ENABLED=1` renders
`AiPlayerbot.Enabled = 1`; the remaining feature toggles are intentionally
disabled by default.

---

## Validation

The pinned baseline has recorded evidence for:

- module-enabled and module-disabled builds
- module loading and world-ready startup
- Headless character login, AI attachment, save / logout / relogin
- same-account human reclaim
- native command dispatch and group invite handling
- Goblin and High Elf lifecycle fixtures
- repeatable module migrations

The current local integrated checkpoint additionally records:

- native Docker build with `BUILD_PLAYERBOTS=OFF`, `MODULES=static` and
  `MODULE_TORTOISEBOTS=static`
- module migration discovery and schema presence
- AI Playerbot initialization and `TortoiseBots: native module loaded (AI enabled)`
- world-ready startup without fatal startup errors
- listening realm and world TCP ports

Still pending:

- upstream merge of core PRs #411 and #416
- manual owned-bot gameplay acceptance
- full 5-player dungeon acceptance
- broader Turtle class/spec/content testing
- large random-bot population testing

The current local Docker source records exact synchronized core/module
revisions in `SOURCE_IDENTITY`.

---

## References

The canonical target core is:

- [Penqle/tortoise-wow](https://github.com/Penqle/tortoise-wow)

Major donor/reference sources include:

- [TortoiseWoW Knowledge Base](https://github.com/tortoise-wow-stack/TortoiseWoWKnowledgeBase)
- [CMaNGOS PlayerBots](https://github.com/cmangos/playerbots)
- [Shyalya/tortoise-wow](https://github.com/Shyalya/tortoise-wow)
- [MangosZero](https://github.com/mangoszero/server)
- [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)

These repositories are behavior and compatibility references, not the target
core. Exact source lineage is recorded in [`docs/PROVENANCE.md`](docs/PROVENANCE.md).

---

## Licence

- **TortoiseBots module** — GPL-2.0 (see donor headers in `ai/playerbot/`)
- **Penqle/tortoise-wow** — AGPL-3.0 — combined binary is AGPL-3.0

See [`LICENCE.md`](LICENCE.md) for full details and donor licences.

---

## Documentation

- [`docs/PLAN.md`](docs/PLAN.md) — architecture and roadmap
- [`docs/HOST_API.md`](docs/HOST_API.md) — core/module host contract
- [`AGENTS.md`](AGENTS.md) — contributor and agent rules
- [`docs/PROVENANCE.md`](docs/PROVENANCE.md) — donor/source lineage
- [`LICENCE.md`](LICENCE.md) — upstream and donor licences

Historical audit evidence remains in Git history and (if retained) under `docs/archive/`.
