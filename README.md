# TortoiseBots

Optional native PlayerBots module for [Tortoise WoW 1.18.1](https://github.com/Penqle/tortoise-wow).
It runs donor PlayerBots behavior (Shyalya/cmangos Turtle baseline plus mod-playerbots) through a small generic core API. Bot logic lives here; session and character lifecycle stays in the core.

> Companion addon (highly recommended to pair with): [TortoiseBotsManager](https://github.com/Sagiroth/TortoiseBotsManager) (`/tbm`). Requires this module on the server.

## Requirements

Plain `Penqle/main` does **not** work. You need the core with [PR #438](https://github.com/Penqle/tortoise-wow/pull/438) merged, or that PR's branch checked out locally. It provides generic Headless sessions (`World::Start/Stop/GetHeadlessSessionState`), character-creation / LFT / BG participant primitives, and removes the legacy in-core PlayerBots.

```bash
git clone https://github.com/Penqle/tortoise-wow.git
cd tortoise-wow
gh pr checkout 438   # or: git fetch origin pull/438/head:pr-438 && git checkout pr-438
```

Contract details: [`docs/HOST_API.md`](docs/HOST_API.md). See the PR for its current head; do not trust a pinned SHA in docs.

## Install and build

```bash
# inside the tortoise-wow checkout from above
git clone https://github.com/Sagiroth/TortoiseBots.git modules/TortoiseBots

cmake -B build -DMODULES=static -DMODULE_TORTOISEBOTS=static
cmake --build build
```

- `MODULES=disabled` still builds the core without bots.
- Config templates: `modules/TortoiseBots/conf/tortoise_bots.conf.dist` installs as `tortoise_bots.conf`; `modules/TortoiseBots/ai/playerbot/aiplayerbot.conf.dist.in` configures to `aiplayerbot.conf`.
- Migrations: `modules/TortoiseBots/data/sql/world/` and `data/sql/char/`.

## Commands

All commands are `.bot <subcommand>` from an in-game character. You may control a bot if you are on its owner account or are a GM; a human logging into a botted character reclaims it from the Headless session.

Lifecycle (names are character names):

- `add <name>` — claim an offline same-account character, persist ownership, queue Headless login. The bot follows you on entry.
- `remove <name>` / `logout <name>` — stop the Headless session asynchronously (`logout` requires the bot to be online). Neither deletes the durable ownership row, so `roster` keeps listing the character.
- `roster` — durable owned characters including offline ones (excludes yourself), emitted as the `TBM:ROSTER_*` stream for the addon.
- `list` — online controllable bots. `stats` — counts of the same set. `status <online bot name>` — lifecycle, movement strategy, AI presence, owner.

Party (each acts on one online owned bot):

- `follow <name>` `stay <name>` `guard <name>` `free <name>` `ready <name>` `attack <name>` `pullback` (`pull-back` alias) `summon <name>` `invite <name>` `uninvite <name>`
- `formation [name] <default|melee|queue|chaos|circle|line|shield|arrow|near|far>` — formation name required, bot name optional.
- `action <intent>` where intent is `attack` `interrupt` `stop` `pull` `pullback` `come` `stay` `follow` `hold` `comestay` `ready` `aoe [on|off]` `focus skull` `cc moon`. `interrupt` selects one owned bot with a ready mature interrupt and the current target actively casting.

Passthrough and help:

- `command <botName> <Playerbot command>` — forwards to the donor PlayerBots parser for the selected bot.
- `help` (alias `h`) prints `TortoiseBots: Enabled`.

## Configuration

The random-population and queue-fill services are **off by default**. The rest of the AI has its own defaults — a key existing in `aiplayerbot.conf.dist.in` does not mean the feature is tested or accepted.

- `AiPlayerbot.Enabled` — master switch.
- `AiPlayerbot.RandomBotAutologin`, `AiPlayerbot.MinRandomBots` / `MaxRandomBots`, `AiPlayerbot.RandomBotAutoCreate`, `AiPlayerbot.EnableRandomTeleports`, `AiPlayerbot.PinnedBots` (commented out = empty/disabled).
- `AiPlayerbot.RandomBotLftEnabled`, `AiPlayerbot.AhMarketEnabled`, `AiPlayerbot.RandomBotBgEnabled` (commented out = disabled).

See that file for exact names, intervals, and batch limits.

## Layout

Main dirs under `modules/TortoiseBots/`:

```text
ai/         donor PlayerBots AI + Turtle compatibility
behavior/   module-owned helpers
commands/   native .bot surface
conf/       module config template
data/sql/   migrations
host/       core <-> module boundary adapters
runtime/    bot lifecycle and AI ownership
src/        module entrypoint (TortoiseBotsModule.cpp)
docs/       HOST_API, PLAN, PROVENANCE, LICENSE_AUDIT, PLAYER_CONTROL
```

Also in tree: `classes/`, `diagnostics/`, `llm/`, `scripts/`, `tools/`. `AGENTS.md` has the contributor workflow.

## Scope and licence

Server-module source only. No client binaries, game data, hosting, or game service. Not affiliated with Blizzard or Turtle WoW.

Mixed original + donor code — follow each file's own notice; do not assume one GPL variant covers all. A combined build with the AGPL-3.0 core must satisfy every component's terms, and the donor compatibility audit is still open. See [`LICENCE.md`](LICENCE.md), [`docs/PROVENANCE.md`](docs/PROVENANCE.md), and [`docs/LICENSE_AUDIT.md`](docs/LICENSE_AUDIT.md).
