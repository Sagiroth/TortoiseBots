# Status

> **Work in progress — integration smoke testing pending.**

TortoiseBots has completed its module-side Vanilla/Turtle cleanup and integration
work, but the current release is **not yet considered plug-and-play against
Penqle `main`**.

Current status:

- [x] Native TortoiseBots module implemented
- [x] Mature PlayerBots AI integrated
- [x] Vanilla/Turtle 1.18.1 cleanup and compatibility audit completed
- [x] Headless session lifecycle implemented and previously runtime-validated
- [x] Legacy PlayerBots-specific core coupling cleaned up in the validated local core
- [x] PlayerBots-enabled and module-disabled builds validated against the local integration baseline
- [ ] Smoke test against the current Penqle modular-core baseline
- [ ] Publish/upstream the remaining required generic Penqle core changes
- [ ] Manual owned-bot gameplay acceptance
- [ ] Manual 5-player dungeon acceptance

The next immediate step is to test the module against the current
**Penqle modular core** and confirm the exact minimal upstream dependency set.

Until that is complete, treat the project as **WIP** and use the exact tested
core/module revisions documented in [`docs/STATUS.md`](docs/STATUS.md).

TortoiseBots has completed its module-side Vanilla/Turtle cleanup and integration
work, but the current release is **not yet considered plug-and-play against
Penqle `main`**.
# Dependencies
TortoiseBots targets:

- **Tortoise WoW 1.18.1**
- **[Penqle/tortoise-wow](https://github.com/Penqle/tortoise-wow)** as the canonical core
- Penqle's native module system
- Generic Headless-session / lifecycle / packet integration required by the module

The intended supported architecture is:

```text
Penqle/tortoise-wow
        |
        | generic module + Headless host capabilities
        v
TortoiseBots

# TortoiseBots

TortoiseBots is an optional native PlayerBots module for [Tortoise WoW 1.18.1](https://github.com/Penqle/tortoise-wow).

It brings mature PlayerBots behavior to the Tortoise/Penqle core while keeping
bot gameplay logic out of normal core systems.

> **Harvest behavior, not architecture.**

The project reuses mature PlayerBots combat, movement, class, group, loot,
quest and travel behavior without recreating the old `GetBot()` / `m_bot` /
`sPlayerBotMgr` coupling model.

## Current status

The module-side Vanilla/Turtle cleanup and deep Turtle 1.18.1 compatibility
audit are complete for the current baseline.

Working pieces include:

- Headless character sessions and same-account owned bots
- Native bot lifecycle management
- Mature `PlayerbotAI` with Engine / Strategy / Trigger / Action / Value
- All nine Vanilla classes
- Follow / stay / group behavior
- Combat and class AI
- Loot, quest, travel and taxi integration
- Native `.bot` command surface
- Packet bridge between Tortoise and `PlayerbotAI`
- Turtle Goblin and High Elf compatibility
- Turtle-specific spell, talent, race and collection-mount handling where
  validated against the target core/data
- Native World / Character migrations
- Bounded random-bot support for pre-existing characters

The physical source tree has been reduced to a Vanilla/Turtle product surface;
large TBC/WotLK/later-era families such as Death Knights, glyphs, vehicles,
arenas and other unsupported expansion systems are not part of the active
module graph.

F-03/F-27 core/data closure is complete for the validated local baseline
(core `7353989c94399f80572a2f8ec2eb73c63a6c79f8`, local branch
`cleanup/f03-f27-code-freeze`):

- **F-03 — closed for the supported local integration:** legacy supported-path
  coupling removed (LFT filler, stale command/stub surface, bot slots, hardwired
  RNDBOT filters, stale include paths and bot-named diagnostics);
  `BUILD_LEGACY_PLAYERBOTS` remains an unsupported historical escape hatch and
  `MODULE_TORTOISEBOTS` is the supported selector. The historical
  `src/modules/PlayerBots` tree is retained disabled, not deleted.
- **F-27 — locally proven where source proves it:** `npc_teslinah` is now
  registered and invalid literal ScriptName `0` is cleared by migration
  `20260825090000_world.sql`; 17 Turtle ScriptNames remain explicitly
  unverified content gaps — not PlayerBots architecture blockers, no fake
  scripts were added.

Broad architecture cleanup is frozen — do not start another generic donor
cleanup. Current next step is manual owned-bot and 5-player dungeon gameplay
acceptance, with the remaining content gaps visible. Upstream Penqle
publication of the core checkpoint is pending (validated local checkpoint, not
yet a merged upstream PR).

See [docs/STATUS.md](docs/STATUS.md) for the exact checkpoints and upstream
status.

## Architecture

TortoiseBots is a native optional module, not a replacement core and not a
vendored PlayerBots core fork.

```text
Tortoise / Penqle core
        |
        | generic Headless / lifecycle / packet / command seams
        v
TortoiseBots
        |
        +-- BotManager
        +-- PlayerbotAIAdapter
        +-- PlayerbotAIStorage
        +-- host adapters
        +-- mature PlayerbotAI
        +-- Strategy / Trigger / Action / Value
        +-- Vanilla/Turtle class and gameplay behavior
```

### Session model

Owned bots use normal Tortoise characters through Headless `WorldSession`s.

```text
One account
    +-- at most one Network session
    +-- zero or more Headless character sessions
```

Network sessions remain account-keyed. Headless sessions are keyed by character
GUID. This preserves ordinary account ownership while allowing a connected
human and owned alternate characters to coexist.

The core owns `WorldSession` lifetime. The module owns bot records and AI.

### Runtime ownership

| Responsibility | Owner |
| --- | --- |
| Network / Headless session lifetime | Tortoise `World` |
| Bot lifecycle records | `BotManager` |
| AI lifetime | `PlayerbotAIAdapter` |
| AI lookup | `PlayerbotAIStorage` |
| Gameplay decisions | `PlayerbotAI` |
| Movement semantics | mature PlayerBots actions/strategies |
| Durable master identity | `BotRecord.masterGuid` |
| Live master pointer | `PlayerbotAI` |
| Packet integration | `BotPacketAdapter` |
| Player lifecycle integration | `BotPlayerAdapter` |
| Chat integration | `BotChatAdapter` |
| Native `.bot` commands | `BotCommands` |
| Mature command language | `PlayerbotAI::HandleCommand` |
| Random population | `RandomBotService` |

The core should not need bot-specific fields or scattered `if (isBot)` gameplay
branches.

## Core integration

The compatible core exposes generic capabilities used by the module:

- `SessionTransport::Network` / `SessionTransport::Headless`
- GUID-keyed Headless session registration and pending login queueing
- generic world/player lifecycle hooks
- generic packet send/receive hooks
- native module loading
- generic command/script integration

Do not reintroduce legacy patterns such as:

```cpp
WorldSession::GetBot()
WorldSession::SetBot()
m_bot
sPlayerBotMgr
PlayerBotEntry
```

The implemented host contract and compatible core baseline are documented in
[docs/HOST_API.md](docs/HOST_API.md).

## Native module layout

Penqle loads TortoiseBots as a native module under:

```text
tortoise-wow/
└── modules/
    └── TortoiseBots/
```

`src/TortoiseBotsModule.cpp` is intentionally the only loader-recursed source.
The actual implementation is registered explicitly from `TortoiseBots.cmake`.
This keeps the native source graph bounded and prevents donor families from
entering the build accidentally.

Important directories:

```text
ai/          Mature PlayerBots AI and Tortoise compatibility
behavior/    Module-owned helpers
commands/    Native .bot command surface
conf/        Module configuration
data/sql/    Module-owned migrations
host/        Tortoise <-> PlayerBots integration boundary
runtime/     Bot lifecycle and AI ownership
src/         Native module entrypoint only
tools/       Audit / developer tooling
docs/        Current architecture, status, audit and provenance
```

## Build selection

The native module is selected by Penqle's module system:

```text
BUILD_LEGACY_PLAYERBOTS=OFF
MODULES=static
MODULE_TORTOISEBOTS=static
```

`BUILD_LEGACY_PLAYERBOTS` is the separate legacy escape hatch and should remain
disabled for normal TortoiseBots work.

For local development, use the persistent builder in the sibling
`tortoise-docker-penqle` checkout. Normal source edits should use the existing
incremental CMake/ccache build rather than rebuilding the full Docker image.
See [AGENTS.md](AGENTS.md) for the working rules.

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

`.bot command` forwards into the mature PlayerBots command handling for the
selected bot. Commands enforce account ownership or GM authority.

## Configuration and data

TortoiseBots installs two configuration surfaces:

- `conf/tortoise_bots.conf.dist` — native module/runtime configuration
- `ai/playerbot/aiplayerbot.conf.dist.in` — mature PlayerBots behavior settings

The inherited PlayerBots configuration is broader than the currently validated
product surface. Random population, economy, social and LLM-related settings
must not be read as proof that every corresponding gameplay path has been
accepted on Turtle 1.18.1.

Module SQL lives under:

```text
data/sql/world/
data/sql/char/
```

Migrations are installed through the native module system. Runtime code should
fail visibly when required data is unavailable rather than silently inventing
schema or pretending unsupported behavior succeeded.

## Validation boundary

The audited baseline includes evidence for:

- native PlayerBots-enabled build
- PlayerBots-disabled / module-absent build
- module load and world-ready startup
- Headless login, AI attachment, save/logout/relogin and cleanup
- same-account reclaim lifecycle
- native command dispatch
- natural group invite / mature AI acceptance
- disposable Goblin and High Elf lifecycle fixtures
- repeatable module migrations

This does **not** claim every gameplay path is complete. In particular, full
manual dungeon acceptance, broad Turtle class/spec interactions, real-client
command delivery, some custom-content paths and large random-bot populations
still need targeted validation.

## Source of truth

For Turtle-specific spells, items, races, locations, scripts and start rows,
use this order:

1. current pinned Tortoise core
2. current Turtle server data / DBC
3. observed runtime behavior and logs
4. Turtle-specific reference implementations
5. Vanilla / donor repositories for comparison only

Do not infer Turtle behavior from Vanilla 1.12 or expansion-era PlayerBots code
when the target core/data can answer the question.

## Source lineage

Major behavior/reference sources include:

- [TortoiseWoW Knowledge Base](https://github.com/tortoise-wow-stack/TortoiseWoWKnowledgeBase) — expected behavior and acceptance ideas
- [CMaNGOS PlayerBots](https://github.com/cmangos/playerbots) — mature combat, movement and class behavior
- [Shyalya/tortoise-wow](https://github.com/Shyalya/tortoise-wow) — Turtle 1.18.1 compatibility lessons
- [MangosZero](https://github.com/mangoszero/server) — lifecycle and native bot-system patterns
- [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) — newer behavior reference

Exact donor commits, source files and copied/ported/reimplemented behavior are
recorded in [docs/PROVENANCE.md](docs/PROVENANCE.md).

## Documentation

Start here:

1. [AGENTS.md](AGENTS.md) — working rules for contributors and coding agents
2. [docs/STATUS.md](docs/STATUS.md) — current baseline, validation and next work
3. [docs/PLAN.md](docs/PLAN.md) — current architecture and roadmap
4. [docs/HOST_API.md](docs/HOST_API.md) — implemented core/module contract
5. [docs/PROVENANCE.md](docs/PROVENANCE.md) — source lineage and validation history
6. [docs/PLAYERBOTS_AUDIT.md](docs/PLAYERBOTS_AUDIT.md) — historical audit evidence

The audit and old handover are historical evidence, not the active execution
plan.
