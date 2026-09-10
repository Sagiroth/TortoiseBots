# TortoiseBots

Independent native PlayerBots module for the canonical **Tortoise WoW 1.18.1** core from Penqle ([`Penqle/tortoise-wow`](https://github.com/Penqle/tortoise-wow), `bot-helpers` branch).

`TortoiseBots` delivers native AI companions through a decoupled C++ architecture: bot AI, combat strategies, and lifecycle management live entirely within this module, while session transport and character state stay cleanly owned by the core via generic headless sessions (`SessionTransport::Headless`). The core builds and runs 100% cleanly without the module (`MODULES=disabled`).

> **Companion in-game UI:** pair with [**TortoiseBotsManager**](https://github.com/tortoise-wow-stack/TortoiseBotsManager) (`/tbm`). All player-facing control — roster, lifecycle, and tactical party actions — is driven from this addon; the server-side command surface is an internal/advanced transport, not a player API.

---

## Features

### Class AI
- Nine Vanilla classes (Warrior–Druid) with class/spec-aware strategies, rotations, and priorities.
- Spec-aware combat: tanking and threat, healing, DPS rotations, ranged/melee positioning.
- Interrupts, crowd control, and raid-mark assignment (`cc <mark>`).
- Buffs, consumables, food/drink and rest, pets, and off-spec support.

### Party & lifecycle
- Owned companions claimed from **your own account**; each runs as a headless session and follows you into the world.
- Summon / follow / stay / formations, plus tactical party intents (attack, pull, pullback, focus, AoE, CC).
- Authoritative roster snapshots (`TBM:` protocol) so the addon always shows real bot state.
- **Human reclaim wins:** a real login on the account always takes precedence over a headless bot.

### World, travel & progression
- Questing, grinding, looting and gathering, training, vendors, resting, and corpse recovery.
- Waypoint / flight-path / boat / zeppelin travel, dungeon and instance entry, and map-aware navigation.
- Progression: leveling, talent presets per spec, gear evaluation and seeding, trainer gating.

### Economy
- Bots use **real inventories** and participate in the live auction house, vendor, and trade flows.
- Optional, default-off bounded auction market service for supply and buyer activity (`AiPlayerbot.ahMarket*`).

### Persistence
- Bot AI state is saved and restored across sessions via the module's AI DB store.
- Durable master binding and character state are owned by the core, so bots survive server restarts and relog with their progress intact.

### Autonomous population & scheduling
- `RandomBotService`: bounded pool of `RNDBOT*` characters with autologin at startup.
- Optional auto-create (default off) that creates accounts/characters toward `MinRandomBots`/`MaxRandomBots` using native core creation APIs.
- Activity leases schedule background work and reconcile shard/realm timing safely.

### Optional services (default off)
- **LFT autofill:** tops up human-waiting Looking-For-Trouble queues with live bots for the missing tank/healer/DPS roles.
- **Battleground auto-queue:** demand-aware WSG / AB / AV participation driven by observed human queue demand.

### Observability (optional)
- Non-blocking UDP telemetry emitter inside the module (`AiPlayerbot.Observability`).
- Standalone Go daemon in [`tools/observability`](tools/observability): Prometheus `/metrics`, live web dashboard (roster, 2D zone map, macro-state breakdown, fleet health, deaths).
- **Persistent issue tracking:** a bot stuck, looping an action, or unable to reach a target for 5+ minutes becomes a tracked episode you can investigate and clear.
- Fully config-gated; zero disk I/O when the daemon is offline.

### Optional LLM (experimental)
- Asynchronous, optional LLM chat integration. **Never required** for combat, movement, healing, threat, interrupts, or CC — bot gameplay continues normally when the LLM is unavailable.

---

## Architecture & host integration

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
                     │ .bot transport / TBM: protocol
  TortoiseBotsManager│ (Client Addon)
  ┌──────────────────┴──────────────────────┐
  │ In-game 1.12 / 11200 UI (/tbm)          │
  │ Tactical Actions & Roster Management    │
  └─────────────────────────────────────────┘
```

- **Clean decoupling:** bot state is never attached to core classes (`WorldSession::GetBot()` / `m_bot` are strictly rejected).
- **Optional native module:** compile with `-DMODULE_TORTOISEBOTS=static` to include; `-DMODULES=disabled` builds a clean core.

---

## Requirements

- A [Tortoise WoW core](https://github.com/Penqle/tortoise-wow) checkout on the `bot-helpers` branch.
- C++17 toolchain and CMake (see build below).
- Optional: the [Docker stack](https://github.com/Sagiroth/tortoise-docker-penqle) for local build/run/telemetry.

---

## Build & development

### 1. Fast iteration (Docker + ccache)

Using [`tortoise-docker-penqle`](https://github.com/Sagiroth/tortoise-docker-penqle):

```bash
# In tortoise-docker-penqle/:
./dev/start              # Start the persistent builder container
./dev/build-playerbots   # Incremental build with ccache (~40s)
./dev/restart-server     # Inject the new mangosd into the live container
./dev/ccache             # Inspect ccache hit rates
```

### 2. Direct CMake build

```bash
# Inside your tortoise-wow (bot-helpers) checkout:
git clone https://github.com/tortoise-wow-stack/TortoiseBots.git modules/TortoiseBots

cmake -B build -DMODULES=static -DMODULE_TORTOISEBOTS=static
cmake --build build -j"$(nproc)"
```

---

## Configuration

Module configuration is generated at build time:

- `conf/tortoise_bots.conf.dist` → installs as `tortoise_bots.conf` (module toggles, service rates, telemetry).
- `ai/playerbot/aiplayerbot.conf.dist.in` → installs as `aiplayerbot.conf` (AI strategies, combat thresholds, economy).

All autonomous/optional services are **disabled by default** and enabled by configuration.

Common toggles (env names as used by the Docker stack):

| Toggle | Purpose |
| :--- | :--- |
| `AI_PLAYERBOT_ENABLED` | Master gameplay on/off; module still loads when off. |
| `RANDOMBOTS_ENABLE` / `RANDOMBOTS_AUTOCREATE` | Autologin the random-bot pool; optionally auto-create the deficit. |
| `MIN_RANDOM_BOTS` / `MAX_RANDOM_BOTS` | Random-bot pool bounds. |
| `OBSERVABILITY_ENABLE` / `OBSERVABILITY_HOST` / `OBSERVABILITY_PORT` | Telemetry emitter and daemon address. |
| `ISSUE_MIN_AGE_SEC` | Only surface persistent bot issues older than this (default 300). |

---

## Repository structure

```text
TortoiseBots/
├── ai/           PlayerBots strategy engine, triggers, actions, multipliers, and class contexts
├── behavior/     Module-owned tactical helpers (CC marks, pullbacks, targeting, convenience)
├── commands/     Native .bot command parsing and dispatch (internal transport)
├── conf/         Module configuration templates
├── data/sql/     Module-owned database migrations (world and character schemas)
├── host/         Generic host adapters (sessions, packets, player binding)
├── runtime/      BotManager, background services, and the observability emitter
├── tools/        Diagnostics, contract verifiers, and the optional observability daemon
└── docs/         Architectural contracts, host seams, provenance, and license records
```

---

## Documentation

- [`docs/PLAN.md`](docs/PLAN.md) — architectural invariants, design rules, and roadmap.
- [`docs/HOST_API.md`](docs/HOST_API.md) — host boundary seams, headless sessions, packets, and lifecycles.
- [`docs/PLAYER_CONTROL.md`](docs/PLAYER_CONTROL.md) — player control intents and addon transport protocol.
- [`tools/observability`](tools/observability) — telemetry daemon, Prometheus metrics, and web dashboard.
- [`docs/PROVENANCE.md`](docs/PROVENANCE.md) — attributions and donor lineage records.
- [`docs/LICENSE_AUDIT.md`](docs/LICENSE_AUDIT.md) — third-party code licensing and compliance.
- [`AGENTS.md`](AGENTS.md) — contributor workflow, dev environment, and engineering rules.

---

## License

This project is a native server module. It contains original code combined with donor PlayerBots implementations under GPL-2.0 / AGPL-3.0 compatible licenses. See [`LICENCE.md`](LICENCE.md), [`docs/PROVENANCE.md`](docs/PROVENANCE.md), and [`docs/LICENSE_AUDIT.md`](docs/LICENSE_AUDIT.md).
