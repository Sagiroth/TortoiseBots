# TortoiseBots

TortoiseBots is an optional native PlayerBots module for
[Tortoise WoW 1.18.1](https://github.com/Penqle/tortoise-wow).

It brings mature PlayerBots combat, movement, class, group, loot, quest and
travel behavior to Tortoise WoW while keeping bot-specific logic outside the
core.

> **Harvest behavior, not architecture.**

TortoiseBots uses Penqle's native module system and a small generic host API
instead of recreating the old tightly coupled `GetBot()` / `m_bot` /
`sPlayerBotMgr` architecture.

---

## Status

> **WIP — Penqle integration smoke testing and upstream host API work are still pending.**

- [x] Native TortoiseBots module implemented
- [x] Mature `PlayerbotAI` integrated
- [x] All nine Vanilla classes included
- [x] Vanilla/Turtle 1.18.1 cleanup and compatibility audit completed
- [x] Headless session lifecycle validated against the local integration baseline
- [x] PlayerBots-enabled and module-disabled builds validated locally
- [ ] Smoke test against the current Penqle modular core
- [ ] Upstream the required generic Headless session API
- [ ] Manual owned-bot gameplay acceptance
- [ ] Manual 5-player dungeon acceptance

The exact currently validated revisions and remaining integration work are
tracked in [`docs/STATUS.md`](docs/STATUS.md).

---

## Penqle dependencies

TortoiseBots targets
[Penqle/tortoise-wow](https://github.com/Penqle/tortoise-wow) as its canonical
core.

### Generic Headless sessions

TortoiseBots needs Penqle to support a `WorldSession` without a network client.

At a high level the core needs to provide:

- Network and Headless session types
- character-GUID keyed Headless session management
- deferred Headless character login
- normal `World` ownership of Headless session lifetime
- generic player/world lifecycle hooks
- generic packet hooks usable by native modules

> [!IMPORTANT]
> **Requires Penqle PR #411 until merged.**
> Build against [`Penqle/tortoise-wow#411`](https://github.com/Penqle/tortoise-wow/pull/411)
> branch `feature/headless-world-session` (`c37e28b`, based on `main` `61a8269`)
> or wait for `main` to include it. Plain `Penqle/main` without #411 does not
> provide `SessionTransport::Headless` / GUID-keyed lifecycle and the module
> will fail to link. Matching module side is `TortoiseBots/integration/penqle-411-baseline` (`19d1934`).

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

Penqle should understand **Headless sessions**, not PlayerBots.

TortoiseBots should **not** require a bot-specific Penqle fork.

Removal of Penqle's historical built-in PlayerBots subsystem
([PR #396](https://github.com/Penqle/tortoise-wow/pull/396)) is desirable for a
clean core, but it is not itself the functional TortoiseBots host API
dependency.

---

## Features

Current module functionality includes:

- Headless character sessions
- Same-account owned bots
- Human reclaim of bot-controlled characters
- Native bot lifecycle management
- Mature `PlayerbotAI`
- Engine / Strategy / Trigger / Action / Value architecture
- All nine Vanilla classes
- Follow / stay / group behavior
- Combat and class AI
- Loot and quest behavior
- Travel and taxi integration
- Native `.bot` commands
- Packet integration between Tortoise and PlayerBots
- Turtle Goblin and High Elf compatibility
- Turtle-aware race, spell, talent and mount handling where validated
- Native World / Character migrations
- Bounded random-bot support for existing characters

The active source tree targets Vanilla/Turtle 1.18.1. Expansion-era systems such
as Death Knights, glyphs, vehicles and arenas are not part of the supported
module graph.

---

## Architecture

TortoiseBots is a native optional module, not a replacement core and not a
vendored PlayerBots core fork.

```text
Penqle / Tortoise core
        |
        | generic Headless / lifecycle / packet / command hooks
        v
TortoiseBots
        |
        +-- BotManager
        +-- PlayerbotAIAdapter
        +-- PlayerbotAIStorage
        +-- host adapters
        +-- mature PlayerbotAI
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

Network sessions remain account-keyed.

Headless sessions are keyed by character GUID.

This allows a connected player and owned alternate characters to coexist
without fake account IDs or dedicated bot accounts.

The core owns `WorldSession` lifetime.

TortoiseBots owns bot records, controllers and AI.

The detailed integration contract is documented in
[`docs/HOST_API.md`](docs/HOST_API.md).

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

`.bot command` forwards into the mature PlayerBots command system for the
selected bot.

Commands enforce normal account ownership or GM authority.

---

## Module layout

TortoiseBots is consumed through Penqle's native module system:

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
host/        Penqle <-> TortoiseBots boundary
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

---

## Validation

The local integration baseline has recorded evidence for:

- module-enabled build
- module-disabled build
- module loading and world-ready startup
- Headless character login
- AI attachment
- save / logout / relogin
- same-account human reclaim
- native command dispatch
- group invite handling
- Goblin and High Elf lifecycle fixtures
- repeatable module migrations

Still pending:

- smoke test against the current Penqle modular core
- manual owned-bot gameplay acceptance
- full 5-player dungeon acceptance
- broader Turtle class/spec/content testing
- large random-bot population testing

See [`docs/STATUS.md`](docs/STATUS.md) for the exact current validation boundary.

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
core.

Exact source lineage is recorded in
[`docs/PROVENANCE.md`](docs/PROVENANCE.md).

---

## Documentation

- [`docs/STATUS.md`](docs/STATUS.md) — current baseline and next work
- [`docs/PLAN.md`](docs/PLAN.md) — architecture and roadmap
- [`docs/HOST_API.md`](docs/HOST_API.md) — Penqle/module host contract
- [`AGENTS.md`](AGENTS.md) — contributor and agent rules
- [`docs/PROVENANCE.md`](docs/PROVENANCE.md) — donor/source lineage
- [`docs/PLAYERBOTS_AUDIT.md`](docs/PLAYERBOTS_AUDIT.md) — historical audit evidence
