# TortoiseBots

Independent native PlayerBots module for canonical Turtle WoW 1.18.1 (`Penqle/tortoise-wow`).

`TortoiseBots` delivers native AI companions through a decoupled C++ architecture: bot AI, combat strategies, and lifecycle management live entirely within this module, while session transport and character state remain cleanly owned by the core server via generic headless sessions (`SessionTransport::Headless`). The core builds and operates 100% cleanly without the module (`MODULES=disabled`).

> **Companion In-Game UI Addon:**
> Pair with [**TortoiseBotsManager**](https://github.com/tortoise-wow-stack/TortoiseBotsManager) (`/tbm`) for client-side party actions and roster management.

---

## Architecture & Host Integration

`TortoiseBots` targets the canonical `bot-helpers` branch of [`Penqle/tortoise-wow`](https://github.com/Penqle/tortoise-wow).

```text
  Penqle Core (tortoise-wow: bot-helpers)
  ┌─────────────────────────────────────────┐
  │ Canonical 1.18.1 World Server           │
  │ Generic Headless Sessions (No bot deps) │
  └──────────────────▲──────────────────────┘
                     │ Generic Seam API
  TortoiseBots Module│ (modules/TortoiseBots)
  ┌──────────────────┴──────────────────────┐
  │ Decoupled Native C++ PlayerBots Engine  │
  │ Class Combat AI, Actions & Strategies   │
  └──────────────────▲──────────────────────┘
                     │ .bot commands / TBM: protocol
  TortoiseBotsManager│ (Client Addon)
  ┌──────────────────┴──────────────────────┐
  │ In-game 1.12 / 11200 UI (/tbm)          │
  │ Tactical Actions & Roster Management    │
  └─────────────────────────────────────────┘
```

- **Clean Decoupling:** Never couples bot state to core classes (`WorldSession::GetBot()` or `m_bot` are strictly rejected).
- **Optional Native Module:** Compile with `-DMODULE_TORTOISEBOTS=static` to include; use `-DMODULES=disabled` to build clean vanilla core.

---

## Build & Development

### 1. Fast Development Iteration (via Docker + ccache)
When using the local Docker environment [`tortoise-docker-penqle`](https://github.com/Sagiroth/tortoise-docker-penqle):
```bash
# In tortoise-docker-penqle/:
./dev/start              # Start persistent builder container
./dev/build-playerbots   # Incremental TU build with ccache (~40s)
./dev/restart-server     # Injects new mangosd into live container and restarts
./dev/ccache             # Inspect ccache hit rates
```

### 2. Direct CMake Build
```bash
# Inside your tortoise-wow (bot-helpers) checkout:
git clone https://github.com/tortoise-wow-stack/TortoiseBots.git modules/TortoiseBots

cmake -B build -DMODULES=static -DMODULE_TORTOISEBOTS=static
cmake --build build -j$(nproc)
```

---

## Player Commands (`.bot`)

All bot interaction runs via in-game `.bot <subcommand>` chat commands (or driven visually by the `/tbm` addon).

### Bot Lifecycle (Same-Account or GM)
- `.bot add <name>` — Claim an offline same-account character; starts a headless session and follows you into the world.
- `.bot remove <name>` / `.bot logout <name>` — Asynchronously logs out the bot (character record remains claimed).
- `.bot roster` — Emits authoritative snapshot of all owned bots and active CC marks (`TBM:ROSTER_*` stream).
- `.bot list` / `.bot stats` — Lists active online bots under your control.
- `.bot status <name>` — Displays bot AI state, movement strategy, and lifecycle status.

### Tactical Party Intents
- `.bot action <intent>` — Dispatches tactical gameplay intents across current target or party:
  - Combat: `attack`, `pull`, `pullback`, `stop`, `interrupt`, `aoe [on|off]`, `focus skull`
  - CC Assignment: `cc <mark>` (e.g. `cc moon`, `cc star`, `cc diamond`, `cc square`)
  - Movement: `come`, `stay`, `follow`, `hold`, `comestay`, `ready`
- `.bot formation <type>` — Sets movement formation (`default`, `melee`, `queue`, `chaos`, `circle`, `line`, `shield`, `arrow`, `near`, `far`).
- `.bot command <botName> <raw command>` — Low-level passthrough directly to the bot's strategy parser.

---

## Configuration

Module configuration is generated at build time:
- `conf/tortoise_bots.conf.dist` → Installs as `tortoise_bots.conf` (module toggles and service rates).
- `ai/playerbot/aiplayerbot.conf.dist.in` → Installs as `aiplayerbot.conf` (bot AI strategies, combat behaviors, and thresholds).

Optional background services (autonomous random bots, LFT autofill, BG autoqueue, synthetic auction house) are **disabled by default** and can be enabled via configuration.

---

## Repository Structure

```text
TortoiseBots/
├── ai/           PlayerBots strategy engine, triggers, actions, multipliers, and class contexts
├── behavior/     Module-owned tactical helpers (CC marks, pullbacks, targeting)
├── commands/     Native .bot chat command parsing and dispatch
├── conf/         Module configuration templates
├── data/sql/     Module-owned database migrations (world and character schemas)
├── host/         Generic host adapters (sessions, packets, player binding)
├── runtime/      BotManager and background service controllers
├── tools/        Diagnostic log checkers, contract verifiers, and surface guards
└── docs/         Architectural contracts, host seams, provenance, and license records
```

---

## Documentation

- [`docs/PLAN.md`](docs/PLAN.md) — Architectural invariants, design rules, and roadmap.
- [`docs/HOST_API.md`](docs/HOST_API.md) — Host boundary seams, headless sessions, network packets, and lifecycles.
- [`docs/PLAYER_CONTROL.md`](docs/PLAYER_CONTROL.md) — Detailed reference for player control intents and addon transport protocols.
- [`docs/PROVENANCE.md`](docs/PROVENANCE.md) — Attributions, donor lineage records, and intent documentation.
- [`docs/LICENSE_AUDIT.md`](docs/LICENSE_AUDIT.md) — Third-party code licensing and compliance tracking.
- [`AGENTS.md`](AGENTS.md) — Central contributor workflow, dev environment guide, and engineering rules.

---

## License

This project is a native server module. It contains original code combined with donor PlayerBots implementations under GPL-2.0 / AGPL-3.0 compatible licenses. See [`LICENCE.md`](LICENCE.md), [`docs/PROVENANCE.md`](docs/PROVENANCE.md), and [`docs/LICENSE_AUDIT.md`](docs/LICENSE_AUDIT.md).
