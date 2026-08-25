# TortoiseBots

TortoiseBots is an optional native PlayerBots module for
[Tortoise WoW 1.18.1](https://github.com/Penqle/tortoise-wow).

It brings mature PlayerBots combat, movement, class, group, loot, quest and
travel behavior to Tortoise WoW while keeping bot-specific gameplay logic out
of the core.

> **Harvest behavior, not architecture.**

The project deliberately avoids the old tightly coupled PlayerBots model built
around core concepts such as `GetBot()`, `m_bot` and `sPlayerBotMgr`.

---

## Status

> **WIP — pending smoke testing against the current Penqle modular core.**

The module-side Vanilla/Turtle port, cleanup and compatibility audit are
complete for the current local integration baseline.

- [x] Native TortoiseBots module implemented
- [x] Mature `PlayerbotAI` integrated
- [x] All nine Vanilla classes included
- [x] Headless session lifecycle implemented and previously runtime-validated
- [x] Vanilla/Turtle 1.18.1 cleanup and compatibility audit completed
- [x] Legacy PlayerBots-specific core coupling cleaned up in the validated local core
- [x] PlayerBots-enabled and module-disabled builds validated locally
- [ ] Smoke test against the current Penqle modular-core baseline
- [ ] Upstream the remaining required generic Penqle core changes
- [ ] Manual owned-bot gameplay acceptance
- [ ] Manual 5-player dungeon acceptance

The next immediate step is to validate TortoiseBots against the current
**Penqle modular core** and confirm the smallest generic host dependency set
that needs to be upstreamed.

Until then, treat the project as WIP and use the exact tested revisions recorded
in [`docs/STATUS.md`](docs/STATUS.md).

---

## Dependencies

TortoiseBots targets:

- **Tortoise WoW 1.18.1**
- **[Penqle/tortoise-wow](https://github.com/Penqle/tortoise-wow)** as the canonical upstream/core
- Penqle's native module system
- Generic Headless session, lifecycle and packet hooks

The intended relationship is:

```text
Penqle/tortoise-wow
        |
        | generic module + Headless host API
        v
TortoiseBots
```

TortoiseBots should **not** require a bot-specific Penqle fork.

Any remaining core changes are intended to be generic capabilities that other
PlayerBots implementations could use as well.

The legacy hard-wired PlayerBots implementation is not a dependency of
TortoiseBots and should remain disabled or removed from the supported core path.

---

## Features

The current module includes:

- Headless character sessions
- Same-account owned bots
- Human reclaim of bot-controlled characters
- Native bot lifecycle management
- Mature `PlayerbotAI`
- Engine / Strategy / Trigger / Action / Value architecture
- All nine Vanilla classes
- Follow / stay / grouping behavior
- Combat and class AI
- Loot and quest behavior
- Travel and taxi integration
- Native `.bot` commands
- Packet bridge between Tortoise and PlayerBots
- Turtle Goblin and High Elf support
- Turtle-aware spell, race, talent and collection-mount handling where validated
- Native World / Character migrations
- Bounded random-bot support for existing characters

The source tree has been reduced to a Vanilla/Turtle product surface. Large
TBC/WotLK/later-era families such as Death Knights, glyphs, vehicles and arenas
are not part of the active module graph.

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
        +-- Vanilla/Turtle gameplay behavior
```

### Session model

Owned bots are normal Tortoise characters using Headless `WorldSession`s.

```text
One account
    +-- at most one Network session
    +-- zero or more Headless character sessions
```

Network sessions remain account-keyed.

Headless sessions are keyed by character GUID.

This allows a connected human and owned alternate characters to coexist without
requiring fake account IDs or separate bot accounts.

The core owns `WorldSession` lifetime.

TortoiseBots owns bot records and AI.

### Runtime ownership

| Responsibility | Owner |
| --- | --- |
| Network / Headless session lifetime | Tortoise `World` |
| Bot records | `BotManager` |
| AI lifetime | `PlayerbotAIAdapter` |
| AI lookup | `PlayerbotAIStorage` |
| Gameplay decisions | `PlayerbotAI` |
| Movement | PlayerBots actions / strategies |
| Master identity | `BotRecord.masterGuid` |
| Packet integration | `BotPacketAdapter` |
| Player lifecycle integration | `BotPlayerAdapter` |
| Chat integration | `BotChatAdapter` |
| Native commands | `BotCommands` |
| Random population | `RandomBotService` |

The core owns sessions.

The module owns bot meaning and behavior.

---

## Core integration

The compatible Penqle core exposes generic capabilities such as:

- `SessionTransport::Network`
- `SessionTransport::Headless`
- GUID-keyed Headless session registration
- pending Headless login handling
- generic world/player lifecycle hooks
- generic packet send/receive hooks
- native module loading
- generic command/script hooks

Normal gameplay code should not need PlayerBots-specific state.

Do not reintroduce legacy patterns such as:

```cpp
WorldSession::GetBot()
WorldSession::SetBot()
m_bot
sPlayerBotMgr
PlayerBotEntry
```

The full host contract is documented in
[`docs/HOST_API.md`](docs/HOST_API.md).

---

## Native module layout

Penqle consumes TortoiseBots as:

```text
tortoise-wow/
└── modules/
    └── TortoiseBots/
```

Important directories:

```text
ai/          PlayerBots AI and Tortoise compatibility
behavior/    Module-owned helpers
commands/    Native .bot commands
conf/        Module configuration
data/sql/    Module migrations
host/        Penqle <-> PlayerBots boundary
runtime/     Bot lifecycle and AI ownership
src/         Native module entrypoint
tools/       Audit / developer tooling
docs/        Architecture, status and provenance
```

`src/TortoiseBotsModule.cpp` is intentionally small.

The wider source graph is registered explicitly through `TortoiseBots.cmake`.

---

## Build selection

Normal native selection:

```text
BUILD_LEGACY_PLAYERBOTS=OFF
MODULES=static
MODULE_TORTOISEBOTS=static
```

`MODULE_TORTOISEBOTS` selects this module.

`BUILD_LEGACY_PLAYERBOTS` refers to the old historical PlayerBots path and
should remain disabled for normal TortoiseBots builds.

For local development, use the persistent builder from
`tortoise-docker-penqle` rather than rebuilding the full Docker image after
normal source edits.

See [`AGENTS.md`](AGENTS.md) for development and validation rules.

---

## Commands

Native commands currently include:

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

`.bot command` forwards commands into the mature PlayerBots command system for
the selected bot.

Authorization is based on normal account ownership or GM authority.

---

## Configuration and data

TortoiseBots provides:

```text
conf/tortoise_bots.conf.dist
ai/playerbot/aiplayerbot.conf.dist.in
```

The inherited PlayerBots configuration contains more options than the currently
validated Turtle product surface. The presence of a config key is not itself a
support guarantee.

Module SQL lives under:

```text
data/sql/world/
data/sql/char/
```

Schema is installed through the native module migration system.

Runtime code should fail visibly when required data is missing rather than
silently inventing schema or pretending unsupported behavior succeeded.

---

## Current integration gaps

The validated local core baseline has already removed the supported-path legacy
PlayerBots coupling.

Two locally provable Turtle ScriptName issues were also corrected:

- `npc_teslinah` registration
- invalid literal ScriptName `0`

Seventeen additional Turtle ScriptNames remain unverified because the current
core does not contain a proven implementation or replacement.

These are treated as **Turtle content gaps**, not PlayerBots architecture
blockers. No fake or no-op scripts are added to hide them.

The exact current integration state is tracked in
[`docs/STATUS.md`](docs/STATUS.md).

---

## Validation

The current local baseline has recorded evidence for:

- native PlayerBots-enabled build
- PlayerBots-disabled / module-absent build
- module loading
- world-ready startup
- Headless login
- AI attachment
- save / logout / relogin
- same-account reclaim
- native command dispatch
- natural group invite / AI acceptance
- Goblin lifecycle fixture
- High Elf lifecycle fixture
- repeatable module migrations

This does **not** mean every gameplay path is complete.

Still pending:

- current Penqle modular-core smoke test
- full owned-bot manual gameplay acceptance
- full 5-player dungeon acceptance
- broad Turtle class/spec/talent testing
- some custom-content paths
- large random-bot population testing

---

## Source of truth

For Turtle-specific behavior, use this order:

1. [Penqle/tortoise-wow](https://github.com/Penqle/tortoise-wow)
2. current Turtle server data / DBC
3. observed runtime behavior and logs
4. Turtle-specific reference implementations
5. Vanilla / donor repositories for comparison only

Do not infer Turtle behavior from Vanilla 1.12 or expansion-era PlayerBots code
when the target core/data already provides the answer.

---

## References and lineage

Major behavior/reference sources include:

- [TortoiseWoW Knowledge Base](https://github.com/tortoise-wow-stack/TortoiseWoWKnowledgeBase)
- [CMaNGOS PlayerBots](https://github.com/cmangos/playerbots)
- [Shyalya/tortoise-wow](https://github.com/Shyalya/tortoise-wow)
- [MangosZero](https://github.com/mangoszero/server)
- [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)

These are donor/reference repositories.

The canonical upstream/core is
[Penqle/tortoise-wow](https://github.com/Penqle/tortoise-wow).

Exact donor commits and copied, ported or reimplemented behavior are tracked in
[`docs/PROVENANCE.md`](docs/PROVENANCE.md).

---

## Documentation

Start here:

1. [`docs/STATUS.md`](docs/STATUS.md) — current tested baseline and next work
2. [`docs/PLAN.md`](docs/PLAN.md) — architecture and roadmap
3. [`docs/HOST_API.md`](docs/HOST_API.md) — Penqle/module integration contract
4. [`AGENTS.md`](AGENTS.md) — contributor and coding-agent rules
5. [`docs/PROVENANCE.md`](docs/PROVENANCE.md) — lineage and validation history
6. [`docs/PLAYERBOTS_AUDIT.md`](docs/PLAYERBOTS_AUDIT.md) — historical audit evidence

Historical audit and handover documents are evidence, not the active execution
plan.
