# TortoiseBots

## What it is

TortoiseBots is an optional native PlayerBots module for Tortoise WoW 1.18.1.
It lets players use owned headless bots while keeping the Tortoise core usable
without the module.

The module owns bot behavior. The core provides only small, generic host
capabilities such as session, lifecycle, packet, and command integration.

## Why it exists

The former PlayerBots implementation coupled bot ownership and bot-specific
logic to normal core systems. TortoiseBots rebuilds that boundary around an
optional module.

The guiding rule is:

> Harvest behavior, not architecture.

## Origin

This repository grew from the clean Tortoise core created after the legacy
PlayerBots removal in [Penqle/tortoise-wow PR #396](https://github.com/Penqle/tortoise-wow/pull/396).

It is a module repository, not a replacement core and not a vendor drop of
another PlayerBots implementation.

## Influences

- [CMaNGOS PlayerBots](https://github.com/cmangos/playerbots) — mature combat, movement, and class behavior
- [MangosZero](https://github.com/mangoszero/server) — lifecycle and native bot-system patterns
- [Shyalya/tortoise-wow](https://github.com/Shyalya/tortoise-wow) — Turtle WoW 1.18.1 compatibility lessons
- [mod-playerbots/mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) — Azeroth Core 3.3.5a Playerbots module

These projects are references for behavior and lessons, not architectures to
copy.

## Initial scope

The first target is a human player using owned bots for normal world content,
with small-party dungeon support as the next step. Optional conversation or
LLM features must never be required for real-time gameplay.

## Development

The module is selected by the target core's native module system with
`MODULE_TORTOISEBOTS=static` or `shared`. Keep the legacy
`BUILD_LEGACY_PLAYERBOTS` path disabled. See the active documentation for the
build, host-boundary, and runtime workflow.

## Start here

1. [AGENTS.md](AGENTS.md)
2. [docs/PLAN.md](docs/PLAN.md)
3. [docs/HOST_API.md](docs/HOST_API.md)
4. [docs/README.md](docs/README.md)
